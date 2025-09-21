#line 1 "C:\\Users\\mwolfram\\Documents\\Arduino Projects\\yoradio\\yoRadio\\src\\async-mqtt-client\\AsyncMqttClient\\MessageProperties.hpp"
#pragma once

struct AsyncMqttClientMessageProperties {
  uint8_t qos;
  bool dup;
  bool retain;
};
