#pragma once

#if __has_include("MqttCredentials.local.h")
#include "MqttCredentials.local.h"
#else
constexpr char MQTT_USERNAME[] = "paw";
constexpr char MQTT_PASSWORD[] = "qwertyuiop";
#endif
