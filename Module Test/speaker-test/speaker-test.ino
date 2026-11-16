#include <Arduino.h>
#include <sdkconfig.h>
#include <esp_arduino_version.h>

#if !defined(CONFIG_IDF_TARGET_ESP32) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Select a classic ESP32 board and install Arduino-ESP32 3.x."
#endif

#include <BluetoothA2DPSink.h>
#include <driver/dac_continuous.h>

constexpr char DEVICE_NAME[] = "Pawbo Speaker Test";
constexpr size_t DAC_CHUNK_SAMPLES = 512;
constexpr int DAC_TIMEOUT_MS = 30;

BluetoothA2DPSink sink;
dac_continuous_handle_t dac = nullptr;
uint16_t dacRate = 0;

bool configureDac(uint16_t rate) {
  if (dac != nullptr && dacRate == rate) {
    return true;
  }
  if (dac != nullptr) {
    dac_continuous_disable(dac);
    dac_continuous_del_channels(dac);
    dac = nullptr;
  }

  dac_continuous_config_t config{};
  config.chan_mask = DAC_CHANNEL_MASK_CH0;  // GPIO25 (Massmore IO1)
  config.desc_num = 8;
  config.buf_size = 1024;
  config.freq_hz = rate;
  config.clk_src = DAC_DIGI_CLK_SRC_APLL;
  config.chan_mode = DAC_CHANNEL_MODE_SIMUL;

  esp_err_t result = dac_continuous_new_channels(&config, &dac);
  if (result == ESP_OK) {
    result = dac_continuous_enable(dac);
    if (result != ESP_OK) {
      dac_continuous_del_channels(dac);
      dac = nullptr;
    }
  }
  if (result != ESP_OK) {
    Serial.printf("DAC init failed: %s\n", esp_err_to_name(result));
    return false;
  }

  dacRate = rate;
  Serial.printf("GPIO25 DAC ready at %u Hz\n", unsigned(rate));
  return true;
}

int32_t readPcm16(const uint8_t* sample) {
  const uint16_t raw = uint16_t(sample[0]) | (uint16_t(sample[1]) << 8);
  return raw < 0x8000 ? int32_t(raw) : int32_t(raw) - 65536;
}

void playPcm(const uint8_t* pcm, uint32_t length) {
  const uint16_t channels = sink.channels();
  const uint16_t rate = sink.sample_rate();
  if ((channels != 1 && channels != 2) || rate == 0 || !configureDac(rate)) {
    return;
  }

  uint8_t samples[DAC_CHUNK_SAMPLES];
  const size_t frameBytes = channels * 2;
  size_t frames = length / frameBytes;
  while (frames > 0) {
    const size_t count = min(frames, sizeof(samples));
    for (size_t i = 0; i < count; ++i) {
      int32_t mono = readPcm16(pcm);
      if (channels == 2) {
        mono = (mono + readPcm16(pcm + 2)) / 2;
      }
      // Convert signed 16-bit PCM to unsigned 8-bit DAC codes.
      samples[i] = uint8_t((mono + 32768) >> 8);
      pcm += frameBytes;
    }
    if (dac_continuous_write(dac, samples, count, nullptr, DAC_TIMEOUT_MS) != ESP_OK) {
      return;  // Drop this packet if the DMA write times out.
    }
    frames -= count;
  }
}

void setup() {
  Serial.begin(115200);
  sink.set_stream_reader(playPcm, false);
  sink.start(DEVICE_NAME, false);
  Serial.printf("Pair with \"%s\" and play media at low volume.\n", DEVICE_NAME);
}

void loop() {
  delay(1000);
}
