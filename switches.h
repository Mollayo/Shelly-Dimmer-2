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
  float &getTemperature();
  uint8 &getSwitchType();
  uint8 &getDefaultSwitchReleaseState();

  void setSwitchType(const char* str);
  void setDefaultSwitchReleaseState(const char* str);
  
  double TaylorLog(double x);
  float readTemperature();
  void configure();
  void setup();
  void handle();
  void handleSWEvent(AceButton* sw, uint8_t eventType, uint8_t buttonState);
}

#endif
