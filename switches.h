#ifndef SWITCHES
#define SWITCHES

#include "wifi.h"
#include "light.h"
#include "config.h"
#include "mqtt.h"


namespace switches {

  enum { LED_UNKNOWN, LED_OFF, LED_FAST_BLINKING, LED_SLOW_BLINKING, LED_ON };

  // For the built-in led blinking
  void enableBuiltinLedBlinking(uint8_t ledMode);
  
  // Getter
  float &getTemperature();
  bool &getTemperatureLogging();
  
  void setSwitchType(const char* str);
  void setDefaultSwitchReleaseState(const char* str);
  
  float readTemperature();
  void updateParams();
  void setup();
  void disableInterrupt();
  void handle();
}

#endif
