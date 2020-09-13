#ifndef CONFIG
#define CONFIG

#include <Arduino.h>


#define SHELLY_BUILTIN_LED 16
#define SHELLY_SW1 14
#define SHELLY_SW2 12
#define SHELLY_BUILTIN_SWITCH 13


#define TEMPERATURE_SENSOR A0

#define STM_NRST_PIN 5
#define STM_BOOT0_PIN 4

#define TOGGLE_BUTTON 2
#define PUSH_BUTTON   1

namespace helpers {

  bool isInteger(const char* str, uint maxLength=10);
  const char* hexToStr(const char *s, uint8_t len);
}

#endif
