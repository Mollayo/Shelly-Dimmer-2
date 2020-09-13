#ifndef DIMMER
#define DIMMER


#include <Arduino.h>

#include "mqtt.h"
#include "wifi.h"
#include "config.h"



namespace dimmer {
  
uint16_t crc(char *buffer, uint8_t len);
void processReceivedPacket(uint8_t payload_cmd, char* payload, uint8_t payload_size);
void receivePacket();
void sendCommand(uint8_t cmd, uint8_t *payload, uint8_t len);
void sendCmdVersion();
void sendCmdBrightness(uint8_t b);
void sendCmdGetState();
void setup();
void handle();

} // namespace dimmer

#endif
