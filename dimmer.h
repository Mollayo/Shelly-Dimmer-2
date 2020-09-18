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

  void setBrightness(uint16_t b);
  void switchOn();
  void switchOff();
  void switchToggle();

  void sendCmdGetVersion();
  void sendCmdGetState();
  void setBlinkingDuration(uint16_t duration);    // in ms, duration==0 means no blinking
  void setup();
  void handle();
  void configure();

} // namespace dimmer

#endif
