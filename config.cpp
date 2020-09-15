#include <Arduino.h>
#include "config.h"

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

  bool isInteger(const char* str, uint maxLength)
  {
    if (str==NULL)
      return false;
    if (strlen(str)==0 || strlen(str)>maxLength)
      return false;
    // Check if every char is a digit
    for (uint8 i=0;i<strlen(str);i++)
      if (str[i]<'0' || str[i]>'9')
        return false;
    return true;
  }

  const char* hexToStr(const char *s, uint8_t len)
  {
    static char output[1000];
    if (len * 3 + 1 > sizeof(output))
    {
      sprintf(output, "buffer overflow in hexToStr");
      return output;
    }
    char *ptr = &output[0];
    int i;
    for (i = 0; i < len - 1; i++) {
      ptr += sprintf(ptr, "%02X_", s[i]);
    }
    if (i < len)
      ptr += sprintf(ptr, "%02X", s[i]);
    return output;
  }
}
