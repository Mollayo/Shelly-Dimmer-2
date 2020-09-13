#ifndef SWITCHES
#define SWITCHES

#include <AceButton.h>
#include "wifi.h"
#include "dimmer.h"
#include "config.h"
#include "mqtt.h"

using namespace ace_button;

namespace switches {

  // Getter
  uint8 &getMinBrightness();
  uint8 &getMaxBrightness();
  uint8 &getBrightness();
  uint8 &getWattage();
  float &getTemperature();
  uint8 &getSwitchType();
  uint8 &getDefaultSwitchReleaseState();

  void setSwitchType(const char* str);
  void setDefaultSwitchReleaseState(const char* str);
  void setMinBrightness(const char* str);
  void setMaxBrightness(const char* str);
  
  void switchOn();
  void switchOff();
  void switchOffOverheat();
  void switchToggle();
  double TaylorLog(double x);
  float readTemperature();
  void configure();
  void setup();
  void handle();
  void handleSWEvent(AceButton* sw, uint8_t eventType, uint8_t buttonState);
}

#endif
