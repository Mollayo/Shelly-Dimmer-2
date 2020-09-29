#ifndef CONFIG
#define CONFIG

#include <Arduino.h>

// For the Shelly Dimmer 2
#define SHELLY_BUILTIN_LED 16
#define SHELLY_SW1 14
#define SHELLY_SW2 12
#define SHELLY_SW0 13             // Built-in switch
#define STM_NRST_PIN 5
#define STM_BOOT0_PIN 4

/*
// For the Shelly 1PM
#define SHELLY_BUILTIN_LED 0
#define SHELLY_SW1 4
//#define SHELLY_SW2 -1           // Not applicable
//#define SHELLY_SW0 2            // Built-in switch -> quite unstable for the Shelly 1PM
#define LIGHT_RELAY 15            // Relay for swtiching on/off the light
*/

#define TEMPERATURE_SENSOR A0   // Correct value


#define TOGGLE_BUTTON 2
#define PUSH_BUTTON   1

// The possible states for the switches
#define BUTTON_RELEASE              0
#define BUTTON_PRESS                1
#define BUTTON_SHORT_CLICK          2
#define BUTTON_LONG_CLICK           3
#define BUTTON_DOUBLE_CLICK         4

#define ALREADY_PUBLISHED           255
#define NO_CHANGE                   255
  
#define LONG_CLICK_DURATION 10      // 500 ms to detect long click


namespace helpers {

  bool isInteger(const char* str, uint maxLength=10);
  const char* hexToStr(const uint8_t *s, uint8_t len);
}

#endif
