#include "RobotMqtt.h"
#include "BrokerCA.h"
#include "MqttCredentials.h"
#include "ServoControl.h"
#include "TouchPages.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <esp_random.h>
#include <mqtt_client.h>
#include <time.h>

namespace RobotMqtt {
namespace {
constexpr char BROKER[] = "mqtts://d6d9f116.ala.asia-southeast1.emqxsl.com:8883";
constexpr char MOTION[] = "pawbo/robot01/cmd/motion";
constexpr char STOP[] = "pawbo/robot01/cmd/stop";
constexpr char ALARM[] = "pawbo/robot01/cmd/alarm";
constexpr char EYES[] = "pawbo/robot01/cmd/eyes";
constexpr char STATUS[] = "pawbo/robot01/status";
constexpr char ACK[] = "pawbo/robot01/ack";
constexpr char ALARM_EVENT[] = "pawbo/robot01/event/alarm";
constexpr char OFFLINE[] = R"({"schema_version":1,"device_id":"robot01","online":false,"robot_state":null,"fault":null})";
constexpr size_t MAX_PAYLOAD = 384;
constexpr time_t MAX_AGE_SECONDS = 5;
constexpr time_t FUTURE_SKEW_SECONDS = 2;
constexpr size_t MAX_ID = 48;
enum class Route : uint8_t { Motion, Stop, Alarm, Eyes };

struct Message {
  Route route;
  size_t length;
  uint32_t session;
  uint32_t sequence;
  char body[MAX_PAYLOAD];
};

struct Receipt {
  char id[MAX_ID + 1];
  const char* status;
  const char* error;
  uint32_t validUntil;
};

esp_mqtt_client_handle_t client = nullptr;
QueueHandle_t commands = nullptr;
QueueHandle_t priorityStop = nullptr;
std::atomic<uint32_t> session{0};
uint32_t sequence = 0;           // Owned by the MQTT event task.
uint32_t subscriptionToken = 0;  // Owned by the MQTT event task.
int subscriptionId = -1;
uint32_t readySession = 0;
uint32_t lastStatus = 0;
uint32_t stopSequence = 0;
uint32_t stopSession = 0;
Receipt receipts[32]{};
Receipt lastStop{};
bool lastAlarmRinging = false;

bool isTopic(const esp_mqtt_event_t& event, const char* topic) {
  return event.topic && event.topic_len == static_cast<int>(strlen(topic)) &&
         memcmp(event.topic, topic, event.topic_len) == 0;
}

void onEvent(void*, esp_event_base_t, int32_t eventId, void* data) {
  const auto& event = *static_cast<esp_mqtt_event_t*>(data);
  switch (eventId) {
    case MQTT_EVENT_CONNECTED: {
      ServoControl::setBrokerConnected(false);
      session.store(0);
      subscriptionToken = esp_random();
      if (!subscriptionToken) subscriptionToken = 1;
      const esp_mqtt_topic_t topics[] = {{MOTION, 0}, {STOP, 1}, {ALARM, 1}, {EYES, 1}};
      subscriptionId = esp_mqtt_client_subscribe_multiple(event.client, topics, 4);
      if (subscriptionId < 0) Serial.println("[MQTT] Subscribe request failed");
      break;
    }
    case MQTT_EVENT_SUBSCRIBED: {
      if (event.msg_id != subscriptionId) break;
      // MQTT 3.1.1 SUBACK contains one return code per requested topic.
      bool granted = event.data && event.data_len == 4;
      for (int i = 0; granted && i < event.data_len; ++i) {
        granted = static_cast<uint8_t>(event.data[i]) <= 2;
      }
      if (granted) {
        ServoControl::setBrokerConnected(true);
        session.store(subscriptionToken);
        Serial.println("[MQTT] PAWBO V1 topics ready");
      } else {
        Serial.println("[MQTT] Check broker subscribe ACL for the four V1 command topics");
      }
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
    case MQTT_EVENT_ERROR:
      ServoControl::setBrokerConnected(false);
      session.store(0);
      break;
    case MQTT_EVENT_DATA: {
      const uint32_t token = session.load();
      if (!token || event.retain || event.current_data_offset || event.data_len <= 0 ||
          event.data_len != event.total_data_len ||
          event.data_len > static_cast<int>(MAX_PAYLOAD)) break;
      Message message{};
      if (isTopic(event, STOP)) message.route = Route::Stop;
      else if (isTopic(event, MOTION)) message.route = Route::Motion;
      else if (isTopic(event, ALARM)) message.route = Route::Alarm;
      else if (isTopic(event, EYES)) message.route = Route::Eyes;
      else break;
      message.length = event.data_len;
      message.session = token;
      message.sequence = ++sequence;
      memcpy(message.body, event.data, message.length);
      if (message.route == Route::Stop) {
        xQueueOverwrite(priorityStop, &message);
      } else if (xQueueSend(commands, &message, 0) != pdPASS) {
        Serial.println("[MQTT] Command queue full");
      }
      break;
    }
    default:
      break;
  }
}

bool start() {
  if (strcmp(MQTT_USERNAME, "YOUR_MQTT_USERNAME") == 0 ||
      strcmp(MQTT_PASSWORD, "YOUR_MQTT_PASSWORD") == 0) return false;
  if (!commands) commands = xQueueCreate(8, sizeof(Message));
  if (!priorityStop) priorityStop = xQueueCreate(1, sizeof(Message));
  if (!commands || !priorityStop) return false;

  esp_mqtt_client_config_t config{};
  config.broker.address.uri = BROKER;
  config.broker.verification.certificate = CA_CERT;
  config.credentials.username = MQTT_USERNAME;
  config.credentials.authentication.password = MQTT_PASSWORD;
  config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
  config.session.keepalive = 10;
  config.session.last_will.topic = STATUS;
  config.session.last_will.msg = OFFLINE;
  config.session.last_will.qos = 1;
  config.session.last_will.retain = true;
  config.network.reconnect_timeout_ms = 5000;
  config.network.timeout_ms = 4000;
  config.buffer.size = 1024;
  config.outbox.limit = 8192;
  client = esp_mqtt_client_init(&config);
  if (!client) return false;
  if (esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, onEvent, nullptr) != ESP_OK ||
      esp_mqtt_client_start(client) != ESP_OK) {
    esp_mqtt_client_destroy(client);
    client = nullptr;
    return false;
  }
  return true;
}

const char* robotState(const ServoControl::Status& servo, bool alarmRinging) {
  if (!servo.ready) return "FAULT";
  if (servo.activeServo) return "MOVING";
  if (alarmRinging) return "ALARM_ACTIVE";
  return "IDLE";
}

bool publish(const char* topic, const JsonDocument& doc, bool retain = false) {
  char payload[512];
  if (measureJson(doc) >= sizeof(payload)) return false;
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  return esp_mqtt_client_enqueue(client, topic, payload, len, 1, retain, true) >= 0;
}

void publishAck(const char* id, const char* status, const char* error = nullptr) {
  JsonDocument reply;
  reply["schema_version"] = 1;
  if (*id) reply["command_id"] = id;
  else reply["command_id"] = nullptr;
  reply["status"] = status;
  reply["robot_state"] = robotState(ServoControl::status(), touchPageStatus().alarmRinging);
  if (error) reply["error_code"] = error;
  else reply["error_code"] = nullptr;
  if (!publish(ACK, reply)) Serial.println("[MQTT] Could not enqueue ACK");
}

void publishStatus(uint32_t token) {
  const auto ui = touchPageStatus();
  const auto servo = ServoControl::status();
  JsonDocument status;
  status["schema_version"] = 1;
  status["device_id"] = "robot01";
  status["online"] = true;
  status["session"] = token;
  status["robot_state"] = robotState(servo, ui.alarmRinging);
  if (servo.ready) status["fault"] = nullptr;
  else status["fault"] = "SERVO_DRIVER";
  status["wifi_rssi"] = WiFi.RSSI();
  status["uptime_s"] = millis() / 1000;
  status["clock_synced"] = time(nullptr) >= 1704067200;
  status["eye_style"] = ui.eyeStyle;
  status["active_servo"] = servo.activeServo;
  if (servo.ready && servo.activeServo) status["target_angle"] = servo.targetAngle;
  else status["target_angle"] = nullptr;
  auto alarm = status["alarm"].to<JsonObject>();
  alarm["hour"] = ui.alarmMinute / 60;
  alarm["minute"] = ui.alarmMinute % 60;
  alarm["enabled"] = ui.alarmEnabled;
  alarm["ringing"] = ui.alarmRinging;
  publish(STATUS, status, true);
}

bool freshTimestamp(const char* timestamp) {
  if (!timestamp || strlen(timestamp) != 20) return false;
  struct tm utc{};
  const char* tail = strptime(timestamp, "%Y-%m-%dT%H:%M:%SZ", &utc);
  if (!tail || *tail) return false;
  using namespace std::chrono;
  const year_month_day date = year{utc.tm_year + 1900} /
      month{static_cast<unsigned>(utc.tm_mon + 1)} / day{static_cast<unsigned>(utc.tm_mday)};
  if (!date.ok() || utc.tm_sec > 59) return false;
  const auto issuedUtc = sys_days{date} + hours{utc.tm_hour} +
      minutes{utc.tm_min} + seconds{utc.tm_sec};
  const time_t issued = static_cast<time_t>(issuedUtc.time_since_epoch().count());
  const time_t age = time(nullptr) - issued;
  return age >= -FUTURE_SKEW_SECONDS && age <= MAX_AGE_SECONDS;
}

Receipt* receiptSlot(const char* id, uint32_t now, bool stop) {
  if (stop) return &lastStop;
  Receipt* available = nullptr;
  for (auto& receipt : receipts) {
    const bool valid = receipt.id[0] && static_cast<int32_t>(receipt.validUntil - now) > 0;
    if (valid && strcmp(receipt.id, id) == 0) return &receipt;
    if (!valid && !available) available = &receipt;
  }
  return available;
}

void handle(const Message& message, uint32_t token) {
  if (message.session != token || session.load() != token || WiFi.status() != WL_CONNECTED) return;
  JsonDocument command;
  const auto parseError = deserializeJson(command, message.body, message.length,
                                           DeserializationOption::NestingLimit(3));
  const char* id = command["command_id"] | "";
  if (strlen(id) > MAX_ID) id = "";
  if (parseError || !command.is<JsonObject>() || !*id ||
      !command["schema_version"].is<int>() || command["schema_version"].as<int>() != 1 ||
      !command["session"].is<uint32_t>() || command["session"].as<uint32_t>() != token) {
    publishAck(id, "rejected", "INVALID_ENVELOPE");
    return;
  }

  const bool isStop = message.route == Route::Stop;
  const uint32_t now = millis();
  Receipt* receipt = receiptSlot(id, now, isStop);
  if (!receipt) {
    publishAck(id, "rejected", "BUSY");
    return;
  }
  if (strcmp(receipt->id, id) == 0 && static_cast<int32_t>(receipt->validUntil - now) > 0) {
    publishAck(id, receipt->status, receipt->error);
    return;
  }
  if (!freshTimestamp(command["issued_at"] | static_cast<const char*>(nullptr))) {
    publishAck(id, "rejected", "STALE_COMMAND");
    return;
  }

  const char* action = command["action"] | "";
  const char* invalid = nullptr;
  int servo = 0;
  int angle = 0;
  int duration = 0;
  int hours = 0;
  int minutes = 0;
  int style = 0;
  bool enabled = false;
  switch (message.route) {
    case Route::Motion:
      servo = command["servo"] | 0;
      angle = command["angle"] | -1;
      duration = command["duration_ms"] | 0;
      if (strcmp(action, "set_servo") || !command["servo"].is<int>() ||
          !command["angle"].is<int>() || !command["duration_ms"].is<int>() ||
          !ServoControl::withinRange(servo, angle) || duration < 100 || duration > 750) {
        invalid = "INVALID_SERVO";
      } else if (stopSession == token && static_cast<int32_t>(message.sequence - stopSequence) <= 0) {
        invalid = "STOPPED";
      } else if (!ServoControl::ready()) invalid = "SERVO_DRIVER";
      else if (touchPageStatus().alarmRinging) invalid = "ALARM_ACTIVE";
      break;
    case Route::Stop:
      if (strcmp(action, "stop")) invalid = "INVALID_ACTION";
      break;
    case Route::Alarm:
      hours = command["hour"] | -1;
      minutes = command["minute"] | -1;
      enabled = command["enabled"] | false;
      if (strcmp(action, "set_alarm") || !command["hour"].is<int>() ||
          !command["minute"].is<int>() || !command["enabled"].is<bool>() ||
          hours < 0 || hours > 23 || minutes < 0 || minutes > 59) invalid = "INVALID_ALARM";
      break;
    case Route::Eyes:
      style = command["style"] | -1;
      if (strcmp(action, "set_eyes") || !command["style"].is<int>() || style < 0 || style >= 6) {
        invalid = "INVALID_EYE_STYLE";
      } else if (touchPageStatus().alarmRinging) invalid = "ALARM_ACTIVE";
      break;
  }

  strlcpy(receipt->id, id, sizeof(receipt->id));
  receipt->validUntil = now + 8000;
  if (invalid) {
    receipt->status = "rejected";
    receipt->error = invalid;
    publishAck(id, receipt->status, receipt->error);
    return;
  }

  // STOP is applied before publishing so network I/O cannot postpone PWM shutoff.
  bool ok = false;
  if (isStop) {
    stopSession = token;
    stopSequence = message.sequence;
    ok = ServoControl::stop();
  }
  publishAck(id, "accepted");
  const char* failure = "SERVO_DRIVER";
  switch (message.route) {
    case Route::Motion:
      if (session.load() != token ||
          !freshTimestamp(command["issued_at"] | static_cast<const char*>(nullptr))) {
        failure = "STALE_COMMAND";
      } else if (uxQueueMessagesWaiting(priorityStop) != 0) {
        failure = "STOP_PENDING";
      } else {
        ok = ServoControl::move(servo, angle, duration);
      }
      break;
    case Route::Stop:
      break;
    case Route::Alarm:
      ok = setDailyAlarm(hours * 60 + minutes, enabled);
      failure = "ALARM_STORAGE";
      break;
    case Route::Eyes:
      ok = selectEyes(style);
      failure = "EYES_BUSY";
      break;
  }
  receipt->status = ok ? "completed" : "failed";
  receipt->error = ok ? nullptr : failure;
  publishAck(id, receipt->status, receipt->error);
  if (session.load() == token) publishStatus(token);
}
}

void tick() {
  if (!client) {
    static uint32_t attemptedAt = 0;
    if (WiFi.status() == WL_CONNECTED && time(nullptr) >= 1704067200 &&
        millis() - attemptedAt >= 5000) {
      attemptedAt = millis();
      if (!start()) Serial.println("[MQTT] Could not start. Check MqttCredentials.h and available memory.");
    }
    return;
  }
  const uint32_t token = session.load();
  if (!token || WiFi.status() != WL_CONNECTED) return;
  if (readySession != token) {
    readySession = token;
    memset(receipts, 0, sizeof(receipts));
    lastStop = {};
    lastStatus = millis() - 1000;
    // A quick reconnect must still clear any previously energized output.
    ServoControl::stop();
  }

  Message message{};
  if (xQueueReceive(priorityStop, &message, 0) == pdPASS) {
    handle(message, token);
  } else if (xQueueReceive(commands, &message, 0) == pdPASS) {
    handle(message, token);
  }

  const bool ringing = touchPageStatus().alarmRinging;
  if (lastAlarmRinging != ringing) {
    JsonDocument event;
    event["schema_version"] = 1;
    event["device_id"] = "robot01";
    event["event"] = ringing ? "started" : "dismissed";
    if (publish(ALARM_EVENT, event)) lastAlarmRinging = ringing;
  }
  if (session.load() == token && millis() - lastStatus >= 1000) {
    lastStatus = millis();
    publishStatus(token);
  }
}
}
