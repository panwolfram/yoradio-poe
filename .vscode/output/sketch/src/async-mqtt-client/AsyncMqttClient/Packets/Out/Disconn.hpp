#line 1 "C:\\Users\\mwolfram\\Documents\\Arduino Projects\\yoradio\\yoRadio\\src\\async-mqtt-client\\AsyncMqttClient\\Packets\\Out\\Disconn.hpp"
#pragma once

#include "OutPacket.hpp"
#include "../../Flags.hpp"
#include "../../Helpers.hpp"

namespace AsyncMqttClientInternals {
class DisconnOutPacket : public OutPacket {
 public:
  DisconnOutPacket();
  const uint8_t* data(size_t index = 0) const;
  size_t size() const;

 private:
  uint8_t _data[2];
};
}  // namespace AsyncMqttClientInternals
