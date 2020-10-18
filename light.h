#ifndef DIMMER
#define DIMMER


#include <Arduino.h>

#include "mqtt.h"
#include "wifi.h"
#include "config.h"



namespace light 
{
  // getter
  uint8_t &getWattage();

  void mqttCallback(const char* paramID, const char* payload);

  void setMinBrightness(const char* str);
  void setMaxBrightness(const char* str);
  void setDimmingParameters(const char* dimmingTypeStr, const char* debounceStr);

  void setBrightness(uint8_t b);
  void lightOn();
  void lightOff();
  ICACHE_RAM_ATTR void lightToggle();
  ICACHE_RAM_ATTR bool lightIsOn();

  void resetSTM32();

  void sendCmdGetVersion();
  void sendCmdGetState();
  void setBlinkingDuration(uint16_t duration);    // in ms, duration==0 means no blinking
  void setup();
  void handle();
  void updateParams();

} // namespace dimmer

#endif
