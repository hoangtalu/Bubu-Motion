#include "sound_system.h"

#include <driver/i2s.h>
#include <cmath>
#include "logger.h"
DEFINE_MODULE_LOGGER(SoundLog)

namespace SoundSystem {

// --------------------------------------------------------------------
// Pin configuration (ESP32-S3 dev board, per board reference)
// MAX98357A wiring:
//   BCLK  -> GPIO5
//   LRCK  -> GPIO4
//   DIN   -> GPIO7
// --------------------------------------------------------------------
static constexpr int PIN_I2S_BCLK = 5;   // I2S BCLK
static constexpr int PIN_I2S_LRCK = 4;   // I2S WS / LRCK
static constexpr int PIN_I2S_DATA = 7;   // I2S DATA OUT (to MAX98357 DIN)

// --------------------------------------------------------------------
// Audio format
// --------------------------------------------------------------------
static constexpr int SAMPLE_RATE = 16000;        // Hz
static constexpr int BLINK_FREQ_HZ = 2000;       // target tone
static constexpr float BLINK_DURATION_SEC = 0.006f; // ~6 ms
static constexpr size_t BLINK_SAMPLES =
    static_cast<size_t>(SAMPLE_RATE * BLINK_DURATION_SEC);
static constexpr int16_t BLINK_PEAK = 50000;
static constexpr bool SOUND_LOGS = true;
static constexpr float kTwoPi = 6.2831853f;

// Eye swoosh defaults
static constexpr float SWOOSH_BASE_DURATION_SEC = 0.040f; // 40 ms baseline
static constexpr float SWOOSH_MAX_DURATION_SEC  = 0.080f; // clamp at 80 ms
static constexpr int   SWOOSH_F_START_HZ = 1400;
static constexpr int   SWOOSH_F_END_HZ   = 700;
static constexpr int16_t SWOOSH_PEAK_MIN = 24000;
static constexpr int16_t SWOOSH_PEAK_MAX = 24000;
static constexpr size_t SWOOSH_MAX_SAMPLES =
    static_cast<size_t>(SAMPLE_RATE * SWOOSH_MAX_DURATION_SEC);

// Jitter noise defaults
static constexpr float JITTER_BASE_DURATION_SEC = 0.012f;
static constexpr float JITTER_MAX_DURATION_SEC  = 0.020f;
static constexpr int16_t JITTER_PEAK_MIN = 24000;
static constexpr int16_t JITTER_PEAK_MAX = 24000;
static constexpr size_t JITTER_MAX_SAMPLES =
    static_cast<size_t>(SAMPLE_RATE * JITTER_MAX_DURATION_SEC);

// Happy pip defaults
static constexpr float HAPPY_PIP_DURATION_SEC = 0.025f; // 25 ms
static constexpr int HAPPY_PIP_BASE_HZ = 900;
static constexpr float HAPPY_PIP_DETUNE = 0.03f; // +/-3%
static constexpr int16_t HAPPY_PIP_PEAK_MIN = 24000;
static constexpr int16_t HAPPY_PIP_PEAK_MAX = 24000;
static constexpr size_t HAPPY_PIP_SAMPLES =
    static_cast<size_t>(SAMPLE_RATE * HAPPY_PIP_DURATION_SEC);

static bool gMuted = false;
static bool gReady = false;
static i2s_port_t gPort = I2S_NUM_0;
// Place audio buffers in PSRAM to free internal RAM
static int16_t* gBlinkBuf = nullptr;
static int16_t* gSwooshBuf = nullptr;
static int16_t* gJitterBuf = nullptr;
static int16_t* gHappyBuf = nullptr; // interleaved L/R

// Build a sine wave with linear decay envelope.
static void buildBlinkBuffer() {
  for (size_t n = 0; n < BLINK_SAMPLES; ++n) {
    float env = 1.0f - (static_cast<float>(n) / static_cast<float>(BLINK_SAMPLES - 1));
    float phase = (kTwoPi * BLINK_FREQ_HZ * static_cast<float>(n)) / static_cast<float>(SAMPLE_RATE);
    float s = sinf(phase);
    int16_t sample = static_cast<int16_t>(s * BLINK_PEAK * env);
    gBlinkBuf[n] = sample;
  }
}

void begin() {
  if (gReady) return;

  gBlinkBuf  = static_cast<int16_t*>(heap_caps_malloc(BLINK_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gSwooshBuf = static_cast<int16_t*>(heap_caps_malloc(SWOOSH_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gJitterBuf = static_cast<int16_t*>(heap_caps_malloc(JITTER_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gHappyBuf  = static_cast<int16_t*>(heap_caps_malloc(HAPPY_PIP_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gBlinkBuf || !gSwooshBuf || !gJitterBuf || !gHappyBuf) {
    SoundLog::println("[Sound] Buffer alloc failed");
    return;
  }

  buildBlinkBuffer();

  i2s_config_t cfg = {
    .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 128,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = PIN_I2S_BCLK,
    .ws_io_num = PIN_I2S_LRCK,
    .data_out_num = PIN_I2S_DATA,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  if (i2s_driver_install(gPort, &cfg, 0, nullptr) != ESP_OK) {
    SoundLog::println("[Sound] i2s_driver_install failed");
    return;
  }
  if (i2s_set_pin(gPort, &pins) != ESP_OK) {
    SoundLog::println("[Sound] i2s_set_pin failed");
    i2s_driver_uninstall(gPort);
    return;
  }
  i2s_zero_dma_buffer(gPort);
  gReady = true;
  if (SOUND_LOGS) {
    SoundLog::printf("[Sound] I2S ready: rate=%dHz, samples=%u, pins BCLK=%d LRCK=%d DATA=%d\n",
                     SAMPLE_RATE,
                     static_cast<unsigned>(BLINK_SAMPLES),
                     PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DATA);
  }
}

void blinkClink() {
  if (gMuted || !gReady) return;
  size_t written = 0;
  // Non-blocking write; if DMA full, drop this click.
  esp_err_t res = i2s_write(gPort, gBlinkBuf, sizeof(gBlinkBuf), &written, 0);
  if (SOUND_LOGS) {
    SoundLog::printf("[Sound] blinkClink res=%d written=%u\n",
                     static_cast<int>(res),
                     static_cast<unsigned>(written));
  }
}

void eyeSwoosh(float strength) {
  if (gMuted || !gReady) return;
  // Clamp strength 0..1
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;

  // Map strength to duration and peak
  float durationSec = SWOOSH_BASE_DURATION_SEC + (SWOOSH_MAX_DURATION_SEC - SWOOSH_BASE_DURATION_SEC) * strength;
  if (durationSec > SWOOSH_MAX_DURATION_SEC) durationSec = SWOOSH_MAX_DURATION_SEC;
  size_t samples = static_cast<size_t>(durationSec * SAMPLE_RATE);
  if (samples < 8) samples = 8;
  if (samples > SWOOSH_MAX_SAMPLES) samples = SWOOSH_MAX_SAMPLES;

  int16_t peak = static_cast<int16_t>(SWOOSH_PEAK_MIN + (SWOOSH_PEAK_MAX - SWOOSH_PEAK_MIN) * strength);

  // Build buffer with downward pitch slide and fast decay envelope
  for (size_t n = 0; n < samples; ++n) {
    float t = static_cast<float>(n) / static_cast<float>(samples - 1); // 0..1
    float freq = SWOOSH_F_START_HZ + (SWOOSH_F_END_HZ - SWOOSH_F_START_HZ) * t;
    float phaseInc = (2.0f * 3.1415926535f * freq) / SAMPLE_RATE;
    float phase = phaseInc * n;
    float s = sinf(phase);
    // envelope: fast attack (~3ms) then decay
    float env;
    float attackSamples = SAMPLE_RATE * 0.003f;
    if (attackSamples < 1.0f) attackSamples = 1.0f;
    if (n < static_cast<size_t>(attackSamples)) {
      env = static_cast<float>(n) / attackSamples;
    } else {
      float decayT = static_cast<float>(n - attackSamples) / static_cast<float>(samples - attackSamples);
      env = 1.0f - decayT; // linear decay
    }
    int16_t sample = static_cast<int16_t>(s * peak * env);
    gSwooshBuf[n] = sample;
  }

  size_t written = 0;
  esp_err_t res = i2s_write(gPort, gSwooshBuf, samples * sizeof(int16_t), &written, 0);
  if (SOUND_LOGS) {
    SoundLog::printf("[Sound] eyeSwoosh strength=%.2f dur_ms=%.1f res=%d written=%u\n",
                     static_cast<double>(strength),
                     static_cast<double>(durationSec * 1000.0f),
                     static_cast<int>(res),
                     static_cast<unsigned>(written));
  }
}

void eyeJitter(float strength) {
  // Short, continuous buzz (sine tone) with tiny envelope
  if (gMuted || !gReady) return;
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;

  float durationSec = JITTER_BASE_DURATION_SEC +
                      (JITTER_MAX_DURATION_SEC - JITTER_BASE_DURATION_SEC) * strength;
  if (durationSec > JITTER_MAX_DURATION_SEC) durationSec = JITTER_MAX_DURATION_SEC;
  size_t samples = static_cast<size_t>(durationSec * SAMPLE_RATE);
  if (samples < 8) samples = 8;
  if (samples > JITTER_MAX_SAMPLES) samples = JITTER_MAX_SAMPLES;

  int16_t peak = static_cast<int16_t>(JITTER_PEAK_MIN +
                   (JITTER_PEAK_MAX - JITTER_PEAK_MIN) * strength);

  const int buzzFreq = 1200; // Hz, steady buzz
  const int periodSamples = SAMPLE_RATE / buzzFreq;
  float attackSamples = SAMPLE_RATE * 0.002f; // ~2ms attack
  if (attackSamples < 1.0f) attackSamples = 1.0f;

  for (size_t n = 0; n < samples; ++n) {
    float phase = (kTwoPi * buzzFreq * static_cast<float>(n)) / static_cast<float>(SAMPLE_RATE);
    float s = sinf(phase);
    float env;
    if (n < static_cast<size_t>(attackSamples)) {
      env = static_cast<float>(n) / attackSamples;
    } else {
      float decayT = static_cast<float>(n - attackSamples) / static_cast<float>(samples - attackSamples);
      env = 1.0f - decayT;
    }
    int16_t sample = static_cast<int16_t>(s * peak * env);
    gJitterBuf[n] = sample;
  }

  size_t written = 0;
  esp_err_t res = i2s_write(gPort, gJitterBuf, samples * sizeof(int16_t), &written, 0);
  if (SOUND_LOGS) {
    SoundLog::printf("[Sound] eyeJitter strength=%.2f dur_ms=%.1f res=%d written=%u\n",
                     static_cast<double>(strength),
                     static_cast<double>(durationSec * 1000.0f),
                     static_cast<int>(res),
                     static_cast<unsigned>(written));
  }
}

void happyPip(float strength) {
  if (gMuted || !gReady) return;
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;

  size_t samples = HAPPY_PIP_SAMPLES;
  int16_t peak = static_cast<int16_t>(HAPPY_PIP_PEAK_MIN +
                   (HAPPY_PIP_PEAK_MAX - HAPPY_PIP_PEAK_MIN) * strength);

  float freqL = HAPPY_PIP_BASE_HZ * (1.0f - HAPPY_PIP_DETUNE);
  float freqR = HAPPY_PIP_BASE_HZ * (1.0f + HAPPY_PIP_DETUNE);
  float incL = (kTwoPi * freqL) / SAMPLE_RATE;
  float incR = (kTwoPi * freqR) / SAMPLE_RATE;
  float phaseL = 0.0f;
  float phaseR = 0.0f;

  for (size_t n = 0; n < samples; ++n) {
    float t = static_cast<float>(n) / static_cast<float>(samples - 1);
    float env = 1.0f - t; // linear decay

    phaseL += incL;
    if (phaseL >= kTwoPi) phaseL -= kTwoPi;
    float sL = sinf(phaseL);

    phaseR += incR;
    if (phaseR >= kTwoPi) phaseR -= kTwoPi;
    float sR = sinf(phaseR);

    int16_t outL = static_cast<int16_t>(sL * peak * env);
    int16_t outR = static_cast<int16_t>(sR * peak * env);
    // interleaved L/R
    gHappyBuf[n * 2]     = outL;
    gHappyBuf[n * 2 + 1] = outR;
  }

  size_t written = 0;
  esp_err_t res = i2s_write(gPort, gHappyBuf, samples * sizeof(int16_t) * 2, &written, 0);
  if (SOUND_LOGS) {
    SoundLog::printf("[Sound] happyPip strength=%.2f dur_ms=%.1f res=%d written=%u\n",
                     static_cast<double>(strength),
                     static_cast<double>(HAPPY_PIP_DURATION_SEC * 1000.0f),
                     static_cast<int>(res),
                     static_cast<unsigned>(written));
  }
}

void mute(bool enabled) {
  gMuted = enabled;
}

}  // namespace SoundSystem
