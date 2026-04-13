#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * C version of the EEP learning and quiz controller.
 * Provide the declared platform functions in your target project.
 */

#define HIGH 1U
#define LOW 0U
#define INPUT_MODE 0U
#define OUTPUT_MODE 1U

#define NUM_BUTTONS 8U
#define SD_CHIP_SELECT 10U
#define SPEAKER_PIN 9U

#define MODE_DEBOUNCE_MS 250U
#define FEEDBACK_DELAY_MS 250U

typedef uint8_t pin_t;

typedef struct AudioHandle AudioHandle;

extern AudioHandle g_audio;

extern void serial_begin(uint32_t baud_rate);
extern void serial_print(const char *text);
extern void serial_println(const char *text);
extern void serial_println_int(int value);
extern void pin_mode(pin_t pin, uint8_t mode);
extern void digital_write(pin_t pin, uint8_t value);
extern uint8_t digital_read(pin_t pin);
extern bool sd_begin(pin_t chip_select);
extern void audio_set_speaker_pin(AudioHandle *audio, pin_t pin);
extern void audio_set_volume(AudioHandle *audio, uint8_t volume);
extern void audio_set_quality(AudioHandle *audio, uint8_t enabled);
extern void audio_play(AudioHandle *audio, const char *file_name);
extern bool audio_is_playing(const AudioHandle *audio);
extern void audio_stop(AudioHandle *audio);
extern uint32_t millis_now(void);
extern uint32_t micros_now(void);
extern void random_seed(uint32_t seed);
extern int random_below(int upper_bound);

enum {
  PIN_A0 = 14,
  PIN_A1 = 15,
  PIN_A2 = 16,
  PIN_A3 = 17,
  PIN_A4 = 18,
  PIN_A5 = 19,
  PIN_A6 = 20
};

static const pin_t touch_pins[NUM_BUTTONS] = {2, 3, 4, 5, 6, 7, 8, PIN_A1};
static const pin_t led_pins[NUM_BUTTONS] = {
  PIN_A0, PIN_A2, PIN_A3, PIN_A4, PIN_A5, 11, 12, 13
};
static const pin_t mode_button = PIN_A6;

static const char *const audio_files[NUM_BUTTONS] = {
  "APPLE.WAV",
  "BALL.WAV",
  "CAT.WAV",
  "DOG.WAV",
  "ONE.WAV",
  "TWO.WAV",
  "RED.WAV",
  "BLUE.WAV"
};

static const char *const quiz_questions[NUM_BUTTONS] = {
  "Q_APPLE.WAV",
  "Q_BALL.WAV",
  "Q_CAT.WAV",
  "Q_DOG.WAV",
  "Q_ONE.WAV",
  "Q_TWO.WAV",
  "Q_RED.WAV",
  "Q_BLUE.WAV"
};

static int last_played = -1;
static int current_question = -1;
static int score = 0;

static bool quiz_mode = false;
static bool mode_button_latched = false;
static bool touch_latched[NUM_BUTTONS] = {false};
static bool waiting_for_next_question = false;

static uint32_t last_mode_change_at = 0U;
static uint32_t feedback_started_at = 0U;

static void clear_all_leds(void) {
  size_t i;

  for (i = 0; i < NUM_BUTTONS; ++i) {
    digital_write(led_pins[i], LOW);
  }
}

static void reset_playback_state(void) {
  audio_stop(&g_audio);
  clear_all_leds();
  last_played = -1;
}

static int get_pressed_button(void) {
  int pressed_index = -1;
  size_t i;

  for (i = 0; i < NUM_BUTTONS; ++i) {
    bool is_pressed = digital_read(touch_pins[i]) == HIGH;

    if (is_pressed && !touch_latched[i] && pressed_index < 0) {
      pressed_index = (int)i;
    }

    touch_latched[i] = is_pressed;
  }

  return pressed_index;
}

static void ask_question(void) {
  int next_question = current_question;

  while (NUM_BUTTONS > 1U && next_question == current_question) {
    next_question = random_below((int)NUM_BUTTONS);
  }

  current_question = next_question;

  serial_print("Question index: ");
  serial_println_int(current_question);
  audio_play(&g_audio, quiz_questions[current_question]);
}

static void update_learning_leds(void) {
  clear_all_leds();

  if (!quiz_mode && audio_is_playing(&g_audio) && last_played >= 0) {
    digital_write(led_pins[last_played], HIGH);
  } else if (!audio_is_playing(&g_audio)) {
    last_played = -1;
  }
}

static void check_mode_switch(void) {
  bool pressed = digital_read(mode_button) == HIGH;

  if (!pressed) {
    mode_button_latched = false;
    return;
  }

  if (mode_button_latched ||
      (millis_now() - last_mode_change_at) < MODE_DEBOUNCE_MS) {
    return;
  }

  mode_button_latched = true;
  last_mode_change_at = millis_now();
  quiz_mode = !quiz_mode;
  waiting_for_next_question = false;
  current_question = -1;

  reset_playback_state();

  if (quiz_mode) {
    score = 0;
    serial_println("QUIZ MODE ON");
    ask_question();
  } else {
    serial_print("LEARNING MODE ON. FINAL SCORE: ");
    serial_println_int(score);
  }
}

static void run_learning_mode(void) {
  int pressed_button = get_pressed_button();

  if (pressed_button < 0 || audio_is_playing(&g_audio)) {
    return;
  }

  last_played = pressed_button;
  audio_play(&g_audio, audio_files[pressed_button]);

  serial_print("Learning: ");
  serial_println(audio_files[pressed_button]);
}

static void run_quiz_mode(void) {
  int pressed_button = get_pressed_button();

  if (waiting_for_next_question) {
    if (!audio_is_playing(&g_audio) &&
        (millis_now() - feedback_started_at) >= FEEDBACK_DELAY_MS) {
      waiting_for_next_question = false;
      ask_question();
    }

    return;
  }

  if (pressed_button < 0 || audio_is_playing(&g_audio)) {
    return;
  }

  if (pressed_button == current_question) {
    ++score;
    serial_print("Correct! Score: ");
    serial_println_int(score);
    audio_play(&g_audio, "CORRECT.WAV");
  } else {
    serial_print("Wrong! Score: ");
    serial_println_int(score);
    audio_play(&g_audio, "WRONG.WAV");
  }

  waiting_for_next_question = true;
  feedback_started_at = millis_now();
}

void setup(void) {
  size_t i;

  serial_begin(9600U);

  for (i = 0; i < NUM_BUTTONS; ++i) {
    pin_mode(touch_pins[i], INPUT_MODE);
    pin_mode(led_pins[i], OUTPUT_MODE);
  }

  pin_mode(mode_button, INPUT_MODE);
  clear_all_leds();

  if (!sd_begin(SD_CHIP_SELECT)) {
    serial_println("SD FAIL!");

    for (;;) {
    }
  }

  audio_set_speaker_pin(&g_audio, SPEAKER_PIN);
  audio_set_volume(&g_audio, 5U);
  audio_set_quality(&g_audio, 1U);

  random_seed(micros_now());
  serial_println("SYSTEM READY");
}

void loop(void) {
  check_mode_switch();

  if (quiz_mode) {
    run_quiz_mode();
  } else {
    run_learning_mode();
  }

  update_learning_leds();
}
