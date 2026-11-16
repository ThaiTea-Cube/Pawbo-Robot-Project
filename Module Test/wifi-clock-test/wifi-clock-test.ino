#include <Arduino.h>
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
constexpr uint32_t SYNC_NOTICE_MS = 30000;
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

enum class State { SavedWifi, Portal, Testing, Saved, Clock };

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
time_t lastDrawn = 0;
std::atomic<bool> clockSynced{false};
bool syncNoticeShown = false;
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

String portalPage(const char* refresh = nullptr) {
  String page = F("<!doctype html><html lang='en'><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>");
  if (refresh) {
    page += F("<meta http-equiv='refresh' content='1;url=");
    page += refresh;
    page += F("'>");
  }
  page += F("<title>Pawbo Wi-Fi</title><style>"
            "body{font:1rem system-ui;margin:6vh auto;padding:0 1rem;max-width:30rem}"
            "label,select,input,textarea,button{display:block;margin:.45rem 0 .8rem}"
            "select,input,textarea,button{font:inherit;padding:.6rem;box-sizing:border-box;width:100%}"
            "label input[type=checkbox]{display:inline;width:auto;margin-right:.5rem}"
            "details{margin:1rem 0}fieldset{border:0;padding:0;margin:0}"
            "</style><h1>Pawbo Wi-Fi</h1>");
  return page;
}

void sendPage(const String& page) {
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/html; charset=utf-8", page);
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
  String page = portalPage("/progress");
  page += F("<p>Connecting Pawbo to your network. Keep this page open.</p>"
            "<p><a href='/progress'>Check connection</a></p>");
  sendPage(page);
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
  String page = portalPage(count == WIFI_SCAN_RUNNING ? "/" : nullptr);
  page += portalMessage;
  if (count == WIFI_SCAN_RUNNING) {
    page += F("<p>Scanning for 2.4 GHz Wi-Fi networks…</p>");
  } else if (count > 0) {
    page += F("<form method='get' action='/network'>"
              "<label for='network'>Choose a Wi-Fi network</label>"
              "<select id='network' name='network' required>"
              "<option value='' selected disabled>Select an SSID</option>");
    for (int i = 0; i < count; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) {
        continue;
      }
      const Security security = networkSecurity(i);
      page += F("<option value='");
      page += scanGeneration;
      page += '-';
      page += i;
      page += security == Security::Unsupported ? "' disabled>" : "'>";
      page += escapeHtml(ssid);
      page += F(" — ");
      page += securityLabel(security);
      page += F(" (");
      page += WiFi.RSSI(i);
      page += F(" dBm)</option>");
    }
    page += F("</select><button type='submit'>Continue</button></form>");
  } else {
    page += F("<p>No networks found. Move Pawbo closer to your router and scan again.</p>");
  }
  if (count != WIFI_SCAN_RUNNING) {
    page += F("<form method='post' action='/scan'><button type='submit'>Scan again</button></form>");
  }
  sendPage(page);
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
  if (state != State::Portal || index < 0 || networkSecurity(index) == Security::Unsupported) {
    redirectToPortal();
    return;
  }
  const Security security = networkSecurity(index);
  String page = portalPage();
  page += F("<p>Network: <strong>");
  page += escapeHtml(WiFi.SSID(index));
  page += F("</strong> (");
  page += securityLabel(security);
  page += F(")</p>");
  page += portalMessage;
  page += F("<form method='post' action='/configure'><input type='hidden' name='network' value='");
  page += web.arg("network"); // selectedNetwork verified the exact numeric format.
  page += F("'><h2>Connection settings</h2>");
  if (security == Security::Peap) {
    page += F("<p>Use PEAP with MSCHAPv2.</p>"
              "<label>Username<input name='username' required maxlength='64' autocapitalize='none'></label>"
              "<label>Outer identity (if supplied by IT)<input name='identity' maxlength='64' autocapitalize='none'></label>");
  }
  if (security == Security::Open) {
    page += F("<p>This network does not require a password.</p>");
  } else if (security == Security::Personal) {
    page += F("<label>Wi-Fi password<input type='password' name='password' required minlength='8' maxlength='64'></label>");
  } else {
    page += F("<label>Password<input type='password' name='password' required maxlength='64'></label>");
  }
  page += F("<details><summary>Advanced settings</summary>");
  if (security == Security::Peap) {
    page += F("<label>Trusted CA certificate (PEM, optional)"
              "<textarea name='ca' maxlength='4096' rows='4' spellcheck='false'></textarea></label>"
              "<p>Leave blank to connect without verifying the authentication server.</p>");
  }
  page += F("<p>Automatic IP (DHCP) is used unless manual IP is selected.</p>"
            "<label><input type='checkbox' name='manual' value='1' id='manual' "
            "onchange=\"document.getElementById('ipfields').hidden=!this.checked;"
            "for(const el of document.querySelectorAll('#ipfields input'))el.required=this.checked\">Use manual IPv4</label>"
            "<fieldset id='ipfields'>"
            "<label>IP address<input name='address' maxlength='15' placeholder='192.168.1.50'></label>"
            "<label>Gateway<input name='gateway' maxlength='15' placeholder='192.168.1.1'></label>"
            "<label>Subnet mask<input name='subnet' maxlength='15' placeholder='255.255.255.0'></label>"
            "<label>DNS server<input name='dns' maxlength='15' placeholder='192.168.1.1'></label>"
            "</fieldset><script>document.getElementById('ipfields').hidden=!document.getElementById('manual').checked;</script>"
            "</details><button type='submit'>Connect and save</button></form><a href='/'>Choose a different network</a>");
  sendPage(page);
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
    showStatus("Setup unavailable", "Restart the board");
    return;
  }
  disconnectWifi();
  snprintf(setupSsid, sizeof(setupSsid), "Pawbo-%06lX",
           static_cast<unsigned long>(ESP.getEfuseMac() >> 24) & 0xffffffUL);
  snprintf(setupPassword, sizeof(setupPassword), "%08lX%04lX",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random() & 0xffff));
  if (!WiFi.softAPConfig(setupIp, setupIp, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(setupSsid, setupPassword)) {
    showStatus("Setup unavailable", "Restart the board");
    return;
  }

  dns.setTTL(0);
  dns.start(53, "*", setupIp);
  web.begin();
  portalOpen = true;
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

void startClock() {
  if (portalOpen) {
    if (!redirectSent) {
      dns.stop();
    }
    web.stop();
    WiFi.softAPdisconnect(true);
    portalOpen = false;
  }
  state = State::Clock;
  stateStarted = millis();
  WiFi.setAutoReconnect(true);
  sntp_set_time_sync_notification_cb([](struct timeval*) { clockSynced.store(true); });
  configTzTime("ICT-7", NTP_HOST);
  showStatus("Syncing Thai time", NTP_HOST);
}

void updateClock() {
  if (!clockSynced.load()) {
    if (!syncNoticeShown && millis() - stateStarted >= SYNC_NOTICE_MS) {
      showStatus("Waiting for NTP", NTP_HOST);
      syncNoticeShown = true;
    }
    return;
  }
  const time_t now = time(nullptr);
  if (now == lastDrawn) {
    return;
  }
  struct tm local;
  if (!localtime_r(&now, &local)) {
    return;
  }
  if (lastDrawn == 0) {
    for (auto* display : displays) {
      display->fillScreen(ST77XX_BLACK);
      centerText(*display, "THAILAND TIME", 42, 2, ST77XX_CYAN);
      centerText(*display, "Royal Thai Navy NTP", 192, 2, ST77XX_CYAN);
    }
  }
  lastDrawn = now;
  char clockText[9];
  char dateText[11];
  strftime(clockText, sizeof(clockText), "%H:%M:%S", &local);
  strftime(dateText, sizeof(dateText), "%d/%m/%Y", &local);
  for (auto* display : displays) {
    centerText(*display, clockText, 95, 4);
    centerText(*display, dateText, 147, 2);
  }
}

void setup() {
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
        startClock();
      } else if (elapsed >= CONNECT_TIMEOUT_MS) {
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
        startClock();
      }
      break;
    case State::Clock:
      updateClock();
      break;
    case State::Portal:
      break;
  }
  delay(10);
}
