#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

constexpr int DFPLAYER_RX_PIN = 13;  // Massmore IO2, receives from DFPlayer TX
constexpr int DFPLAYER_TX_PIN = 14;  // Massmore IO4, sends to DFPlayer RX
constexpr int BARK_TRACK = 1;  // First file indexed by DFPlayer on the card
constexpr uint8_t MIN_VOLUME = 0;
constexpr uint8_t MAX_VOLUME = 30;

HardwareSerial playerSerial(2);
DFRobotDFPlayerMini player;
uint8_t volume = 8;
bool playerReady = false;

void playBark() {
  player.play(BARK_TRACK);
  Serial.println("Play requested: track 1 (expected /0001.mp3)");
}

void setup() {
  Serial.begin(115200);
  playerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);

  Serial.println("Initializing DFPlayer Mini...");
  if (!player.begin(playerSerial)) {
    Serial.println("DFPlayer init failed. Check 5V, common GND, TX/RX and the microSD card.");
    Serial.println("After checking the connections, reset the ESP32 to retry.");
    return;
  }

  playerReady = true;
  player.volume(volume);
  Serial.printf("Startup volume: %u/30\n", unsigned(volume));
  Serial.println("p: play bark | x: stop | + / -: change volume");
  playBark();
}

void loop() {
  if (!playerReady) {
    delay(50);
    return;
  }

  if (player.available()) {
    const uint8_t type = player.readType();
    const uint16_t value = player.read();
    if (type == DFPlayerPlayFinished) {
      Serial.println("Playback finished.");
    } else if (type == DFPlayerError) {
      Serial.printf("DFPlayer error %u. Check /0001.mp3 and the card.\n", unsigned(value));
    } else if (type == TimeOut) {
      Serial.println("DFPlayer UART timeout: no reply in time. Try p at low volume and check 5V/GND and UART.");
    }
  }

  switch (Serial.read()) {
    case 'p':
      playBark();
      break;
    case 'x':
      player.stop();
      break;
    case '+':
      if (volume < MAX_VOLUME) {
        ++volume;
        player.volume(volume);
      }
      Serial.printf("Volume: %u/30\n", unsigned(volume));
      break;
    case '-':
      if (volume > MIN_VOLUME) {
        --volume;
        player.volume(volume);
      }
      Serial.printf("Volume: %u/30\n", unsigned(volume));
      break;
  }
  delay(5);
}
