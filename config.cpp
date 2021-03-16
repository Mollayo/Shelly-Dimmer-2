#include <Arduino.h>
#include "config.h"


namespace helpers {

  bool isInteger(const char* str, uint8_t maxLength)
  {
    if (str==NULL)
      return false;
    uint8_t len=strlen(str);
    if (len==0 || len>maxLength || len>10)
      return false;
      
    // Check if every char is a digit
    for (uint8_t i=0;i<strlen(str);i++)
      if (str[i]<'0' || str[i]>'9')
        return false;
    return true;
  }
  
  bool convertToInteger(const char* str, uint16_t &val, uint8_t maxLength)
  {
    if (str==NULL)
      return false;
    uint8_t len=strlen(str);
    if (len==0 || len>maxLength || len>10)
      return false;
      
    // Convert the text to number
    static char output[10];
    uint8_t c1=0;
    while(c1<len && (str[c1]<'0' || str[c1]>'9'))
      c1++;
    uint8_t c2=0;
    while(c1+c2<len && str[c1+c2]>='0' && str[c1+c2]<='9')
    {
      output[c2]=str[c1+c2];
      c2++;
    }
    if (c2>0)
    {
      output[c2]='\0';
      val=atoi(output);
      return true;
    }
    return false;
  }

  const char* hexToStr(const uint8_t *s, uint8_t len)
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
