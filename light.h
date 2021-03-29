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
  ICACHE_RAM_ATTR void lightOn(bool noLightAutoTurnOff=false);
  ICACHE_RAM_ATTR void lightOff();
  ICACHE_RAM_ATTR void lightToggle(bool noLightAutoTurnOff=false);
  ICACHE_RAM_ATTR bool lightIsOn();

  void STM32reset();

  void sendCmdGetVersion();
  void sendCmdGetState();
  void setBlinkingDuration(const char* durationStr);
  void setBlinkingPattern(const char *payload);
  void startBlinking();
  void stopBlinking();
  void setup();
  void handle();
  void updateParams();

  void addWifiManagerCustomButtons();
  void addWifiManagerCustomParams();
  void bindServerCallback();

} // namespace dimmer

#endif
