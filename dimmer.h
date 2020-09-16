#ifndef DIMMER
#define DIMMER


#include <Arduino.h>

#include "mqtt.h"
#include "wifi.h"
#include "config.h"



namespace dimmer 
{
  // getter
  uint16_t &getMinBrightness();
  uint16_t &getMaxBrightness();
  uint16_t &getBrightness();
  uint8 &getWattage();

  void setMinBrightness(const char* str);
  void setMaxBrightness(const char* str);
  void setDimmingParameters(const char* dimmingTypeStr, const char* debounceStr);

  void switchOn();
  void switchOff();
  void switchToggle();

  
  uint16_t crc(char *buffer, uint8_t len);
  void processReceivedPacket(uint8_t payload_cmd, char* payload, uint8_t payload_size);
  void receivePacket();
  void sendCommand(uint8_t cmd, uint8_t *payload, uint8_t len);
  void sendCmdGetVersion();
  void sendCmdSetBrightness(uint16_t b);
  void sendCmdGetState();
  void sendCmdSetDimmingParameters(uint8_t dimmingType,uint8_t debounce);
  void setup();
  void handle();
  void configure();

} // namespace dimmer

#endif
