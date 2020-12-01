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

  void STM32reset();

  void sendCmdGetVersion();
  void sendCmdGetState();
  void setBlinkingDuration(uint16_t duration);    // in ms, duration==0 means no blinking
  void setup();
  void handle();
  void updateParams();

  // For the firmware flashing
  void addWifiManagerParameters();
  void bindServerCallback();
  bool STM32FlashBegin();
  void STM32FlashEnd();
  bool STM32FlashUpload(const uint8_t data[], unsigned int size);

} // namespace dimmer

#endif
