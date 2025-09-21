#line 1 "C:\\Users\\mwolfram\\Documents\\Arduino Projects\\yoradio\\yoRadio\\src\\async-mqtt-client\\AsyncMqttClient\\Packets\\Packet.hpp"
#pragma once

namespace AsyncMqttClientInternals {
class Packet {
 public:
  virtual ~Packet() {}

  virtual void parseVariableHeader(char* data, size_t len, size_t* currentBytePosition) = 0;
  virtual void parsePayload(char* data, size_t len, size_t* currentBytePosition) = 0;
};
}  // namespace AsyncMqttClientInternals
