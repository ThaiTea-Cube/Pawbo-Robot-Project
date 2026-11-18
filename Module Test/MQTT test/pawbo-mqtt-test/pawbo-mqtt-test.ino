#include <Arduino.h>
#include "TouchPages.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <WiFi.h>
#include <atomic>
#include <esp_wifi.h>
#if __has_include(<esp_eap_client.h>)
#include <esp_eap_client.h>
#else
#include <esp_wpa2.h>
#endif
#include <esp_random.h>
#include <esp_sntp.h>
#include <qrcode.h>
#include <time.h>

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <cstring>

namespace mqttTest {
constexpr char HOST[] = "d6d9f116.ala.asia-southeast1.emqxsl.com";
constexpr char USERNAME[] = "paw";
constexpr char PASSWORD[] = "qwertyuiop";
constexpr char COMMAND[] = "robot01/cmd";
constexpr char STATUS[] = "robot01/status";
constexpr char ACK[] = "robot01/ack";
constexpr char TELEMETRY[] = "robot01/telemetry";
constexpr char CA_CERT[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBhMQswCQYDVQQG
EwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3d3cuZGlnaWNlcnQuY29tMSAw
HgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBHMjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUx
MjAwMDBaMGExCzAJBgNVBAYTAlVTMRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3
dy5kaWdpY2VydC5jb20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkq
hkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI2/Ou8jqJ
kTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx1x7e/dfgy5SDN67sH0NO
3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQq2EGnI/yuum06ZIya7XzV+hdG82MHauV
BJVJ8zUtluNJbd134/tJS7SsVQepj5WztCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyM
UNGPHgm+F6HmIcr9g+UQvIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQAB
o0IwQDAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV5uNu
5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY1Yl9PMWLSn/pvtsr
F9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4NeF22d+mQrvHRAiGfzZ0JFrabA0U
WTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NGFdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBH
QRFXGU7Aj64GxJUTFy8bJZ918rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/
iyK5S9kJRaTepLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)PEM";

WiFiClientSecure transport;
PubSubClient client(transport);
uint32_t lastAttempt = 0;
uint32_t lastTelemetry = 0;

void onMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, COMMAND) != 0) return;
  if (length == 5 && memcmp(payload, "hello", 5) == 0) {
    Serial.println("[MQTT] hello received");
    if (!client.publish(STATUS, "received", false)) Serial.println("[MQTT] hello reply failed");
    return;
  }

  JsonDocument command;
  const auto error = deserializeJson(command, static_cast<const byte*>(payload), length,
                                     DeserializationOption::NestingLimit(4));
  const char* action = command["action"] | "";
  const bool knownAction = strcmp(action, "forward") == 0 || strcmp(action, "backward") == 0 ||
                           strcmp(action, "left") == 0 || strcmp(action, "right") == 0 ||
                           strcmp(action, "stop") == 0;
  const int left = command["left"] | -1;
  const int right = command["right"] | -1;
  const bool validId = command["id"].is<uint32_t>() && command["id"].as<uint32_t>() > 0;
  const bool accepted = !error && command.is<JsonObject>() && validId && knownAction &&
                        command["left"].is<int>() && command["right"].is<int>() &&
                        left >= 0 && left <= 255 && right >= 0 && right <= 255;

  JsonDocument reply;
  if (validId) reply["id"] = command["id"].as<uint32_t>();
  else reply["id"] = nullptr;
  reply["accepted"] = accepted;
  reply["mode"] = "communication_only";
  if (accepted) {
    const bool stop = strcmp(action, "stop") == 0;
    reply["action"] = action;
    reply["left"] = stop ? 0 : left;
    reply["right"] = stop ? 0 : right;
  } else {
    reply["error"] = "invalid_command";
  }
  char message[256];
  serializeJson(reply, message, sizeof(message));
  Serial.printf("[MQTT] ACK %s\n", message);
  if (!client.publish(ACK, message, false)) Serial.println("[MQTT] ACK publish failed");
}

void begin() {
  transport.setCACert(CA_CERT);
  transport.setHandshakeTimeout(4);
  client.setServer(HOST, 8883);
  client.setCallback(onMessage);
  if (!client.setBufferSize(1024)) Serial.println("[MQTT] Could not allocate requested buffer");
  client.setSocketTimeout(4);
  if (strcmp(USERNAME, "YOUR_MQTT_USERNAME") == 0 || strcmp(PASSWORD, "YOUR_MQTT_PASSWORD") == 0)
    Serial.println("[MQTT] Set USERNAME and PASSWORD in the sketch");
}

void tick() {
  if (strcmp(USERNAME, "YOUR_MQTT_USERNAME") == 0 || strcmp(PASSWORD, "YOUR_MQTT_PASSWORD") == 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    transport.stop();
    return;
  }
  const uint32_t now = millis();
  if (!client.connected()) {
    if (now - lastAttempt < 5000) return;
    lastAttempt = now;
    if (time(nullptr) < 1700000000) {
      Serial.println("[MQTT] Waiting for NTP before TLS");
      return;
    }
    char id[24];
    snprintf(id, sizeof(id), "pawbo-%012llX", static_cast<unsigned long long>(ESP.getEfuseMac()));
    if (!client.connect(id, USERNAME, PASSWORD, STATUS, 0, true, "offline")) {
      char tlsError[120] = {};
      const int tlsCode = transport.lastError(tlsError, sizeof(tlsError));
      Serial.printf("[MQTT] Connect failed: state=%d TLS=%d %s\n", client.state(), tlsCode, tlsError);
      return;
    }
    if (!client.subscribe(COMMAND, 0) || !client.publish(STATUS, "online", true)) {
      transport.stop();
      return;
    }
    Serial.println("[MQTT] Connected; subscription requested for robot01/cmd");
  }

  if (!client.loop() || now - lastTelemetry < 1000) return;
  lastTelemetry = now;
  char message[144];
  snprintf(message, sizeof(message),
           "{\"device\":\"ESP32\",\"uptime_s\":%lu,\"wifi_rssi\":%ld,\"mode\":\"communication_only\"}",
           static_cast<unsigned long>(now / 1000), static_cast<long>(WiFi.RSSI()));
  client.publish(TELEMETRY, message, false);
}
}  // namespace mqttTest


constexpr int TFT_SCK = 18;
constexpr int TFT_MOSI = 23;
constexpr int TFT_RST = 4;
constexpr int TFT_DC = 2;
constexpr int TFT1_CS = 32;
constexpr int TFT2_CS = 33;
constexpr int TFT_SIZE = 240;
constexpr int TFT_ROTATION = 2;

constexpr uint32_t SETTINGS_MAGIC = 0x50415732;
constexpr uint32_t LEGACY_SETTINGS_MAGIC = 0x50415731;
constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t HANDOFF_TIMEOUT_MS = 12000;
constexpr uint32_t REDIRECT_FLUSH_MS = 600;
constexpr char NTP_HOST[] = "time.navy.mi.th";
constexpr char SETUP_ADDRESS[] = "192.168.4.1";
constexpr char SETUP_URL[] = "http://192.168.4.1/";
constexpr char AFTER_SETUP_URL[] = "https://kmutt.ac.th/";
const IPAddress setupIp(192, 168, 4, 1);

enum class Security : uint8_t { Unsupported, Open, Personal, Peap };

struct LegacyWifiSettings {
  uint32_t magic;
  char ssid[33];
  char password[65];
};

struct WifiSettings {
  uint32_t magic;
  char ssid[33];
  char password[65];
  char username[65];
  char identity[65];
  char caCert[4097];
  Security security;
  uint8_t manualIp;
  uint32_t address;
  uint32_t gateway;
  uint32_t subnet;
  uint32_t dns;
};

enum class State { SavedWifi, Portal, Testing, Saved, TouchPages };

bool validSettings(const WifiSettings& settings);
bool connectWifi(const WifiSettings& settings, State nextState, const uint8_t* bssid = nullptr);
void sendNetworkForm();
Security networkSecurity(int index);
void centerText(Adafruit_ST7789& display, const char* label, int y, uint8_t size,
                uint16_t color = ST77XX_WHITE);

Adafruit_ST7789 tft1(&SPI, TFT1_CS, TFT_DC, -1);
Adafruit_ST7789 tft2(&SPI, TFT2_CS, TFT_DC, -1);
Adafruit_ST7789* const displays[] = {&tft1, &tft2};
DNSServer dns;
WebServer web(setupIp, 80);
State state = State::Portal;
WifiSettings candidate{};
bool eepromReady = false;
bool portalOpen = false;
uint32_t stateStarted = 0;
std::atomic<bool> clockSynced{false};
char setupSsid[16];
char setupPassword[13];
const char* portalMessage = "";
uint32_t scanGeneration = 0;
uint32_t redirectedAt = 0;
bool redirectSent = false;
bool enterpriseEnabled = false;

void centerText(Adafruit_ST7789& display, const char* label, int y, uint8_t size,
                uint16_t color) {
  display.setTextSize(size);
  display.setTextColor(color, ST77XX_BLACK);
  const int width = static_cast<int>(strlen(label)) * 6 * size;
  display.setCursor((display.width() - width) / 2, y);
  display.print(label);
}

void showStatus(const char* title, const char* detail) {
  for (auto* display : displays) {
    display->fillScreen(ST77XX_BLACK);
    centerText(*display, title, 85, 2, ST77XX_CYAN);
    centerText(*display, detail, 125, 2);
  }
}

Adafruit_ST7789* qrDisplay = nullptr;

void renderQr(esp_qrcode_handle_t qr) {
  constexpr int scale = 4;
  constexpr int border = 4;
  constexpr int areaTop = 33;
  constexpr int areaHeight = 164;
  const int modules = esp_qrcode_get_size(qr);
  const int side = (modules + 2 * border) * scale;
  const int left = (qrDisplay->width() - side) / 2;
  const int top = areaTop + (areaHeight - side) / 2;

  qrDisplay->startWrite();
  qrDisplay->writeFillRect(left, top, side, side, ST77XX_WHITE);
  for (int y = 0; y < modules; ++y) {
    for (int x = 0; x < modules; ++x) {
      if (esp_qrcode_get_module(qr, x, y)) {
        qrDisplay->writeFillRect(left + (x + border) * scale,
                                 top + (y + border) * scale, scale, scale, ST77XX_BLACK);
      }
    }
  }
  qrDisplay->endWrite();
}

void drawQr(Adafruit_ST7789& display, const char* payload, const char* title,
            const char* footer, const char* detail) {
  display.fillScreen(ST77XX_BLACK);
  centerText(display, title, 9, 2, ST77XX_CYAN);

  esp_qrcode_config_t config{};
  config.display_func = renderQr;
  config.max_qrcode_version = 4;
  config.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  // esp_qrcode_generate calls renderQr before returning.
  qrDisplay = &display;
  const esp_err_t result = esp_qrcode_generate(&config, payload);
  qrDisplay = nullptr;

  if (result != ESP_OK) {
    centerText(display, "QR unavailable", 103, 2);
  }
  centerText(display, footer, 203, 2);
  centerText(display, detail, 225, 1);
}

bool validSettings(const WifiSettings& settings) {
  const size_t ssidLength = strnlen(settings.ssid, sizeof(settings.ssid));
  const size_t passwordLength = strnlen(settings.password, sizeof(settings.password));
  if (settings.magic != SETTINGS_MAGIC || ssidLength == 0 || ssidLength >= sizeof(settings.ssid) ||
      passwordLength >= sizeof(settings.password) || settings.manualIp > 1) {
    return false;
  }
  switch (settings.security) {
    case Security::Open:
      return passwordLength == 0;
    case Security::Personal:
      return (passwordLength >= 8 && passwordLength <= 63) ||
             (passwordLength == 64 && strspn(settings.password, "0123456789abcdefABCDEF") == 64);
    case Security::Peap:
      return passwordLength > 0 && settings.username[0] &&
             strnlen(settings.username, sizeof(settings.username)) < sizeof(settings.username) &&
             strnlen(settings.identity, sizeof(settings.identity)) < sizeof(settings.identity) &&
             strnlen(settings.caCert, sizeof(settings.caCert)) < sizeof(settings.caCert) &&
             (settings.caCert[0] == '\0' ||
              (strstr(settings.caCert, "-----BEGIN CERTIFICATE-----") &&
               strstr(settings.caCert, "-----END CERTIFICATE-----")));
    default:
      return false;
  }
}

bool loadSettings() {
  EEPROM.get(0, candidate);
  if (validSettings(candidate)) {
    return true;
  }
  LegacyWifiSettings legacy{};
  EEPROM.get(0, legacy);
  if (legacy.magic != LEGACY_SETTINGS_MAGIC ||
      strnlen(legacy.ssid, sizeof(legacy.ssid)) == sizeof(legacy.ssid) ||
      strnlen(legacy.password, sizeof(legacy.password)) == sizeof(legacy.password)) {
    return false;
  }
  memset(&candidate, 0, sizeof(candidate));
  candidate.magic = SETTINGS_MAGIC;
  candidate.security = legacy.password[0] ? Security::Personal : Security::Open;
  strcpy(candidate.ssid, legacy.ssid);
  strcpy(candidate.password, legacy.password);
  return validSettings(candidate);
}

void disconnectWifi() {
  // Cancel association even if the STA has not connected yet.
  esp_wifi_disconnect();
  if (enterpriseEnabled) {
#if __has_include(<esp_eap_client.h>)
    esp_wifi_sta_enterprise_disable();
    esp_eap_client_clear_ca_cert();
#else
    esp_wifi_sta_wpa2_ent_disable();
    esp_wifi_sta_wpa2_ent_clear_ca_cert();
#endif
    enterpriseEnabled = false;
  }
}

bool connectWifi(const WifiSettings& settings, State nextState, const uint8_t* bssid) {
  disconnectWifi();
  if (settings.manualIp) {
    if (!WiFi.config(IPAddress(settings.address), IPAddress(settings.gateway),
                     IPAddress(settings.subnet), IPAddress(settings.dns))) {
      return false;
    }
  } else if (!WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0))) {
    return false;
  }
  WiFi.setMinSecurity(settings.security == Security::Open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK);

  if (settings.security == Security::Peap) {
    const char* identity = settings.identity[0] ? settings.identity : settings.username;
    const char* caCert = settings.caCert[0] ? settings.caCert : nullptr;
    enterpriseEnabled = true;
    WiFi.begin(settings.ssid, WPA2_AUTH_PEAP, identity, settings.username,
               settings.password, caCert, nullptr, nullptr, -1, 0, bssid);
  } else {
    const char* password = settings.security == Security::Open ? nullptr : settings.password;
    WiFi.begin(settings.ssid, password, 0, bssid);
  }
  // WiFi.begin reports the current status, which can still reflect a prior attempt.
  state = nextState;
  stateStarted = millis();
  return true;
}

Security networkSecurity(int index) {
  switch (WiFi.encryptionType(index)) {
    case WIFI_AUTH_OPEN:
      return Security::Open;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return Security::Personal;
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return Security::Peap;
    default:
      return Security::Unsupported;
  }
}

const char* securityLabel(Security security) {
  switch (security) {
    case Security::Open: return "Open";
    case Security::Personal: return "WPA2/WPA3 Personal";
    case Security::Peap: return "Enterprise (PEAP)";
    default: return "Unsupported";
  }
}

String escapeHtml(const String& value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

const char PORTAL_HEAD[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pawbo Wi-Fi setup</title>
)html";

const char PORTAL_STYLE[] PROGMEM = R"html(<style>
:root{color-scheme:light;--ink:#1d2621;--muted:#526058;--line:#dce3db;--accent:#285843}
*{box-sizing:border-box}
body{margin:0;background:#f2f5ef;color:var(--ink);font:16px/1.55 system-ui,sans-serif;padding:clamp(1rem,7vh,4rem) 1rem}
main{max-width:30rem;margin:auto;background:#fff;border:1px solid var(--line);border-radius:1.35rem;padding:clamp(1.25rem,5vw,2.25rem);box-shadow:0 .75rem 2.5rem #1735280e}
header{margin-bottom:1.35rem}
.brand{font-size:.83rem;font-weight:750;letter-spacing:.12em;text-transform:uppercase;color:var(--accent)}
h1{font-size:clamp(1.65rem,6vw,2.25rem);line-height:1.12;letter-spacing:-.04em;margin:.45rem 0 .7rem}
p{margin:.45rem 0 .9rem;color:var(--muted)}
label{display:block;font-weight:600;margin:.85rem 0}
select,input,textarea,button{font:inherit}
select,input:not([type=checkbox]),textarea{width:100%;padding:.67rem .75rem;border:1px solid #bcc9be;border-radius:.55rem;background:#f9faf8;color:var(--ink)}
label input:not([type=checkbox]),label textarea{display:block;margin-top:.32rem}
textarea{resize:vertical}
button{width:100%;border:1px solid transparent;border-radius:.55rem;background:var(--accent);color:white;font-weight:650;padding:.63rem .8rem;cursor:pointer}
.secondary{background:white;color:var(--accent);border-color:var(--line);margin-top:.65rem}
a{color:var(--accent);font-weight:650;text-underline-offset:.18em}
.back{display:inline-block;margin-top:.9rem}
:focus-visible{outline:3px solid #528d70;outline-offset:2px}
details{border-top:1px solid var(--line);border-bottom:1px solid var(--line);margin:1.1rem 0;padding:.65rem 0}
summary{cursor:pointer;font-weight:650}
fieldset{border:0;padding:0;margin:0;min-width:0}
#manual:not(:checked)~fieldset{display:none}
#manual{accent-color:var(--accent)}
label[for=manual]{display:inline;margin-left:.3rem}
[role=alert]{padding:.6rem .8rem;background:#fcf0e9;border-radius:.55rem;color:#793819}
.network{background:#edf4ed;border-radius:.65rem;padding:.65rem .8rem;overflow-wrap:anywhere}
.network strong{display:block;color:var(--ink)}
</style></head><body><main><header><div class="brand">Pawbo</div><h1>)html";

void beginPortalPage(PGM_P heading, const char* refresh) {
  web.sendHeader("Cache-Control", "no-store");
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200, "text/html; charset=utf-8", "");
  web.sendContent_P(PORTAL_HEAD);
  if (refresh) {
    char meta[80];
    snprintf(meta, sizeof(meta), "<meta http-equiv='refresh' content='1;url=%s'>", refresh);
    web.sendContent(meta);
  }
  web.sendContent_P(PORTAL_STYLE);
  web.sendContent_P(heading);
  web.sendContent_P(PSTR("</h1></header>"));
}

void endPortalPage() {
  web.sendContent_P(PSTR("</main></body></html>"));
  web.sendContent("");
}

void redirectTo(const char* url) {
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("Location", url);
  web.send(303, "text/plain; charset=utf-8", url);
}

void redirectToPortal() {
  redirectTo(SETUP_URL);
}

void connectionProgress() {
  if (state == State::Saved) {
    if (!redirectSent) {
      dns.stop();
    }
    redirectTo(AFTER_SETUP_URL);
    if (!redirectSent) {
      redirectedAt = millis();
      redirectSent = true;
    }
    return;
  }
  if (state != State::Testing) {
    redirectToPortal();
    return;
  }
  beginPortalPage(PSTR("Connecting Pawbo"), "/progress");
  web.sendContent_P(PSTR("<p>Keep this page open while Pawbo joins your network.</p>"
                         "<a href='/progress'>Check connection</a>"));
  endPortalPage();
}

void beginScan() {
  ++scanGeneration;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
}

void rescan() {
  if (state == State::Portal && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    beginScan();
  }
  redirectToPortal();
}

void sendPortal() {
  if (state == State::Testing || state == State::Saved) {
    redirectTo("/progress");
    return;
  }
  const int count = WiFi.scanComplete();
  beginPortalPage(PSTR("Connect to Wi-Fi"), count == WIFI_SCAN_RUNNING ? "/" : nullptr);
  if (*portalMessage) {
    web.sendContent(portalMessage);
  }
  if (count == WIFI_SCAN_RUNNING) {
    web.sendContent_P(PSTR("<p>Scanning for nearby 2.4 GHz networks…</p>"));
  } else if (count > 0) {
    web.sendContent_P(PSTR("<form method='get' action='/network'>"
                           "<label for='network'>Choose your network</label>"
                           "<select id='network' name='network' required>"
                           "<option value='' selected disabled>Select an SSID</option>"));
    for (int i = 0; i < count; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) {
        continue;
      }
      const Security security = networkSecurity(i);
      const String safeSsid = escapeHtml(ssid);
      char option[288];  // A 32-byte SSID expands to at most 192 escaped bytes.
      snprintf(option, sizeof(option), "<option value='%lu-%d'%s>%s — %s (%ld dBm)</option>",
               static_cast<unsigned long>(scanGeneration), i,
               security == Security::Unsupported ? " disabled" : "", safeSsid.c_str(),
               securityLabel(security), static_cast<long>(WiFi.RSSI(i)));
      web.sendContent(option);
    }
    web.sendContent_P(PSTR("</select><button type='submit'>Continue</button></form>"));
  } else {
    web.sendContent_P(PSTR("<p>No networks found. Move Pawbo closer to your router and scan again.</p>"));
  }
  if (count != WIFI_SCAN_RUNNING) {
    web.sendContent_P(PSTR("<form method='post' action='/scan'>"
                           "<button class='secondary' type='submit'>Scan again</button></form>"));
  }
  endPortalPage();
}

int selectedNetwork() {
  const String key = web.arg("network");
  const int separator = key.indexOf('-');
  const int count = WiFi.scanComplete();
  if (separator <= 0 || count <= 0) {
    return -1;
  }
  const long index = key.substring(separator + 1).toInt();
  const String expected = String(scanGeneration) + '-' + String(index);
  return key == expected && index >= 0 && index < count ? static_cast<int>(index) : -1;
}

void sendNetworkForm() {
  const int index = selectedNetwork();
  if (state != State::Portal || index < 0 || WiFi.SSID(index).isEmpty() ||
      networkSecurity(index) == Security::Unsupported) {
    redirectToPortal();
    return;
  }
  const Security security = networkSecurity(index);
  beginPortalPage(PSTR("Network settings"), nullptr);
  web.sendContent_P(PSTR("<p class='network'><strong>"));
  web.sendContent(escapeHtml(WiFi.SSID(index)));
  web.sendContent_P(PSTR("</strong>"));
  web.sendContent(securityLabel(security));
  web.sendContent_P(PSTR("</p>"));
  if (*portalMessage) {
    web.sendContent(portalMessage);
  }
  web.sendContent_P(PSTR("<form method='post' action='/configure'><input type='hidden' name='network' value='"));
  web.sendContent(web.arg("network"));  // selectedNetwork verified the numeric format.
  web.sendContent_P(PSTR("'>"));
  if (security == Security::Peap) {
    web.sendContent_P(PSTR("<label>Username<input name='username' required maxlength='64' "
                           "autocomplete='username' autocapitalize='none'></label>"));
  }
  if (security == Security::Open) {
    web.sendContent_P(PSTR("<p>This network is open. You can connect now.</p>"));
  } else if (security == Security::Personal) {
    web.sendContent_P(PSTR("<label>Wi-Fi password<input type='password' name='password' required minlength='8' "
                           "maxlength='64' autocomplete='current-password'></label>"));
  } else {
    web.sendContent_P(PSTR("<label>Password<input type='password' name='password' required maxlength='64' "
                           "autocomplete='current-password'></label>"));
  }
  web.sendContent_P(PSTR("<details><summary>Advanced settings</summary>"));
  if (security == Security::Peap) {
    web.sendContent_P(PSTR("<label>Outer identity (optional)"
                           "<input name='identity' maxlength='64' autocapitalize='none'></label>"
                           "<label>Trusted CA certificate (PEM, optional)"
                           "<textarea name='ca' maxlength='4096' rows='4' spellcheck='false'></textarea></label>"
                           "<p>Leave blank to connect without verifying the authentication server.</p>"));
  }
  web.sendContent_P(PSTR("<p>Automatic IP (DHCP) is used by default.</p>"
                         "<input type='checkbox' name='manual' value='1' id='manual'>"
                         "<label for='manual'>Use manual IPv4</label><fieldset id='ipfields'>"
                         "<label>IP address<input name='address' maxlength='15' placeholder='192.168.1.50'></label>"
                         "<label>Gateway<input name='gateway' maxlength='15' placeholder='192.168.1.1'></label>"
                         "<label>Subnet mask<input name='subnet' maxlength='15' placeholder='255.255.255.0'></label>"
                         "<label>DNS server<input name='dns' maxlength='15' placeholder='192.168.1.1'></label>"
                         "</fieldset></details><button type='submit'>Connect and save</button></form>"
                         "<a class='back' href='/'>Choose a different network</a>"));
  endPortalPage();
}

bool copyField(const String& value, char* output, size_t capacity) {
  if (value.length() >= capacity || strlen(value.c_str()) != value.length()) {
    return false;
  }
  value.toCharArray(output, capacity);
  return true;
}

uint32_t ipv4Number(const IPAddress& address) {
  return (uint32_t(address[0]) << 24) | (uint32_t(address[1]) << 16) |
         (uint32_t(address[2]) << 8) | uint32_t(address[3]);
}

bool validUnicast(const IPAddress& address) {
  return address[0] > 0 && address[0] < 224 && address[0] != 127;
}

bool readManualIp(WifiSettings& settings) {
  IPAddress address, gateway, subnet, dnsAddress;
  if (!address.fromString(web.arg("address").c_str()) || !gateway.fromString(web.arg("gateway").c_str()) ||
      !subnet.fromString(web.arg("subnet").c_str()) || !dnsAddress.fromString(web.arg("dns").c_str()) ||
      address.type() != IPv4 || gateway.type() != IPv4 || subnet.type() != IPv4 || dnsAddress.type() != IPv4 ||
      !validUnicast(address) || !validUnicast(gateway) || !validUnicast(dnsAddress)) {
    return false;
  }
  const uint32_t ip = ipv4Number(address);
  const uint32_t gw = ipv4Number(gateway);
  const uint32_t mask = ipv4Number(subnet);
  const uint32_t hosts = ~mask;
  // WiFi.config expects a legacy-order subnet whose first octet is 255.
  if (subnet[0] != 255 || hosts < 3 || (hosts & (hosts + 1)) ||
      (ip & mask) != (gw & mask) || ip == gw ||
      (ip & hosts) == 0 || (ip & hosts) == hosts || (gw & hosts) == 0 || (gw & hosts) == hosts) {
    return false;
  }
  settings.manualIp = 1;
  settings.address = static_cast<uint32_t>(address);
  settings.gateway = static_cast<uint32_t>(gateway);
  settings.subnet = static_cast<uint32_t>(subnet);
  settings.dns = static_cast<uint32_t>(dnsAddress);
  return true;
}

void receiveSettings() {
  const int index = selectedNetwork();
  if (state != State::Portal || index < 0) {
    redirectToPortal();
    return;
  }
  memset(&candidate, 0, sizeof(candidate));
  candidate.magic = SETTINGS_MAGIC;
  candidate.security = networkSecurity(index);
  String caCert = web.arg("ca");
  caCert.trim();
  if (!copyField(WiFi.SSID(index), candidate.ssid, sizeof(candidate.ssid)) ||
      (candidate.security != Security::Open &&
       !copyField(web.arg("password"), candidate.password, sizeof(candidate.password))) ||
      (candidate.security == Security::Peap &&
       (!copyField(web.arg("username"), candidate.username, sizeof(candidate.username)) ||
        !copyField(web.arg("identity"), candidate.identity, sizeof(candidate.identity)) ||
        !copyField(caCert, candidate.caCert, sizeof(candidate.caCert)))) ||
      !validSettings(candidate)) {
    portalMessage = "<p role='alert'>Check the credentials and certificate for this network.</p>";
    sendNetworkForm();
    return;
  }
  if (web.arg("manual") == "1" && !readManualIp(candidate)) {
    portalMessage = "<p role='alert'>Check the IPv4 address, gateway, subnet mask and DNS.</p>";
    sendNetworkForm();
    return;
  }

  if (!connectWifi(candidate, State::Testing, WiFi.BSSID(index))) {
    portalMessage = "<p role='alert'>Could not start the connection. Check the settings and try again.</p>";
    sendNetworkForm();
    return;
  }
  portalMessage = "";
  redirectTo("/progress");
}

void startPortal() {
  if (!WiFi.mode(WIFI_AP_STA)) {
    Serial.println("[Wi-Fi] Could not enable AP+STA mode");
    showStatus("Setup unavailable", "Restart the board");
    return;
  }
  disconnectWifi();
  snprintf(setupSsid, sizeof(setupSsid), "Pawbo-%06lX",
           static_cast<unsigned long>(ESP.getEfuseMac() >> 24) & 0xffffffUL);
  snprintf(setupPassword, sizeof(setupPassword), "%08lX%04lX",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random() & 0xffff));
  if (!WiFi.softAPConfig(setupIp, setupIp, IPAddress(255, 255, 255, 0))) {
    Serial.println("[Wi-Fi] Could not configure the setup AP address");
    showStatus("Setup unavailable", "AP address failed");
    return;
  }
  if (!WiFi.softAP(setupSsid, setupPassword)) {
    Serial.println("[Wi-Fi] Could not start the setup AP");
    showStatus("Setup unavailable", "AP start failed");
    return;
  }

  dns.setTTL(0);
  dns.start(53, "*", setupIp);
  web.begin();
  portalOpen = true;
  Serial.printf("[Wi-Fi setup] SSID: %s\n", setupSsid);
  Serial.printf("[Wi-Fi setup] Password: %s\n", setupPassword);
  Serial.printf("[Wi-Fi setup] Open %s after joining the setup Wi-Fi\n", SETUP_URL);
  char wifiQr[64];
  snprintf(wifiQr, sizeof(wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", setupSsid, setupPassword);
  char passwordHint[28];
  snprintf(passwordHint, sizeof(passwordHint), "Password: %s", setupPassword);
  drawQr(tft1, wifiQr, "1. Join Pawbo Wi-Fi", setupSsid, passwordHint);
  tft2.fillScreen(ST77XX_BLACK);
  centerText(tft2, "SET UP PAWBO", 16, 3, ST77XX_CYAN);
  centerText(tft2, "1. Scan QR on TFT1", 66, 2);
  centerText(tft2, "2. Join Pawbo Wi-Fi", 96, 2);
  centerText(tft2, "3. Select your Wi-Fi", 126, 2);
  centerText(tft2, "No setup page? Open", 173, 2);
  centerText(tft2, SETUP_ADDRESS, 201, 3, ST77XX_CYAN);
  beginScan();
}

void startTouchPages() {
  if (portalOpen) {
    if (!redirectSent) {
      dns.stop();
    }
    web.stop();
    WiFi.scanDelete();
    WiFi.softAPdisconnect(true);
    portalOpen = false;
  }
  state = State::TouchPages;
  WiFi.setAutoReconnect(true);
  sntp_set_time_sync_notification_cb([](struct timeval*) { clockSynced.store(true); });
  configTzTime("ICT-7", NTP_HOST);
  enterTouchPages(clockSynced.load());
}

void setup() {
  Serial.begin(115200);
  Serial.println("[Pawbo] Starting TFT displays");
  pinMode(TFT1_CS, OUTPUT);
  pinMode(TFT2_CS, OUTPUT);
  digitalWrite(TFT1_CS, HIGH);
  digitalWrite(TFT2_CS, HIGH);
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);
  SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);
  for (auto* display : displays) {
    display->init(TFT_SIZE, TFT_SIZE, SPI_MODE0);
    display->setRotation(TFT_ROTATION);
    display->setTextWrap(false);
  }
  showStatus("PAWBO", "Starting Wi-Fi");
  Serial.println("[Pawbo] TFT initialization complete");
  mqttTest::begin();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  web.on("/", HTTP_GET, sendPortal);
  web.on("/network", HTTP_GET, sendNetworkForm);
  web.on("/scan", HTTP_POST, rescan);
  web.on("/configure", HTTP_POST, receiveSettings);
  web.on("/progress", HTTP_GET, connectionProgress);
  web.onNotFound(redirectToPortal);

  eepromReady = EEPROM.begin(sizeof(WifiSettings));
  if (eepromReady && loadSettings() && connectWifi(candidate, State::SavedWifi)) {
    Serial.println("[Wi-Fi] Trying saved network for up to 30 seconds");
    showStatus("Connecting Wi-Fi", "Using saved network");
  } else {
    startPortal();
  }
}

void loop() {
  if (portalOpen) {
    dns.processNextRequest();
    web.handleClient();
  }

  const uint32_t now = millis();
  const uint32_t elapsed = now - stateStarted;
  switch (state) {
    case State::SavedWifi:
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        startTouchPages();
      } else if (elapsed >= CONNECT_TIMEOUT_MS) {
        Serial.println("[Wi-Fi] Saved network timed out; starting setup AP");
        state = State::Portal;
        startPortal();
      }
      break;
    case State::Testing:
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        if (eepromReady) {
          EEPROM.put(0, candidate);
        }
        if (eepromReady && EEPROM.commit()) {
          state = State::Saved;
          stateStarted = millis();
          showStatus("Wi-Fi saved", "Opening next page...");
        } else {
          disconnectWifi();
          portalMessage = "<p role='alert'>Could not save Wi-Fi to EEPROM. Please try again.</p>";
          state = State::Portal;
        }
      } else if (elapsed >= CONNECT_TIMEOUT_MS) {
        disconnectWifi();
        portalMessage = "<p role='alert'>Could not connect. Check the credentials and IP settings.</p>";
        state = State::Portal;
      }
      break;
    case State::Saved:
      if ((redirectSent && now - redirectedAt >= REDIRECT_FLUSH_MS) || elapsed >= HANDOFF_TIMEOUT_MS) {
        startTouchPages();
      }
      break;
    case State::TouchPages:
      tickTouchPages(clockSynced.load());
      mqttTest::tick();
      break;
    case State::Portal:
      break;
  }
  delay(10);
}

