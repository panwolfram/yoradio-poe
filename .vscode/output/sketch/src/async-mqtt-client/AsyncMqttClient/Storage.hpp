#line 1 "C:\\Users\\mwolfram\\Documents\\Arduino Projects\\yoradio\\yoRadio\\src\\async-mqtt-client\\AsyncMqttClient\\Storage.hpp"
#pragma once

namespace AsyncMqttClientInternals {
struct PendingPubRel {
  uint16_t packetId;
};

struct PendingAck {
  uint8_t packetType;
  uint8_t headerFlag;
  uint16_t packetId;
};
}  // namespace AsyncMqttClientInternals
