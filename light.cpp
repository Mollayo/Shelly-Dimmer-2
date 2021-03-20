#include <Arduino.h>

#include "mqtt.h"
#include "wifi.h"
#include "logging.h"
#include "config.h"
#include "light.h"
#include "switches.h"
#include "stm32flash.h"



namespace light {

// The light parameters
volatile uint8_t minBrightness = 0;   // brightness values in %
volatile uint8_t maxBrightness = 50;
volatile uint8_t brightness = 0;
uint8_t publishedBrightness = 0;      // The last brigthness value published to MQTT
uint8_t wattage = 0;

// For the auto-off timer
uint16_t autoOffDuration = 0;         // In seconds
volatile unsigned long lastLightOnTime = 0;
volatile bool lightAutoTurnOffDisable = false;

// For blinking
unsigned long startBlinkingTime = 0;
uint16_t blinkingTimerDuration = 5;  // In seconds
bool blinking = false;

// For the blinking pattern
unsigned long lastBlinkingLightStateTime = 0;
bool blinkingLightState = false;
uint16_t blinkingPattern[10] = {500, 500, 0, 0, 0, 0, 0, 0, 0, 0};   // in ms; if 0, no blinking



uint8_t &getWattage() {
  return wattage;
}

WiFiManagerParameter wifiManagerCustomButtons[] = 
{
  // Button for the firmware update
  WiFiManagerParameter("<form action=\"/uploadSTM32Firmware\"><input type=\"submit\" value=\"Update the STM32 firmware\"></form>"),
};

// The dimmer parameters
WiFiManagerParameter wifiManagerCustomParams[] = 
{
  WiFiManagerParameter("<br/><br/><hr><h3>Light parameters</h3>"),
  WiFiManagerParameter("minBrightness", "Minimum brightness (0% to 20%)", "0", 3),
  WiFiManagerParameter("maxBrightness", "Maximum brightness (0% to 100%)", "50", 3),
  WiFiManagerParameter("autoOffTimer", "Auto-off timer (value in seconds)", "", 3),
  WiFiManagerParameter("dimmingType", "Dimming type (0: trailing edge (LED), 1: leading edge (halogen))", "0", 1),
  WiFiManagerParameter("flickerDebounce", "Anti-flickering debounce (50 - 150)", "100", 3),
};

const uint8_t CMD_SET_BRIGHTNESS = 0x02;
const uint8_t CMD_SET_BRIGHTNESS_ADVANCED = 0x03;
const uint8_t CMD_GET_STATE = 0x10;
const uint8_t CMD_GET_VERSION = 0x01;
const uint8_t CMD_SET_DIMMING_PARAMETERS = 0x20;
const uint8_t CMD_SET_DIMMING_TYPE_2 = 0x30;
const uint8_t CMD_SET_DIMMING_TYPE_3 = 0x31;
const uint8_t CMD_SET_WARM_UP_TIME = 0x21;

#define LEADING_EDGE 0x01
#define TRAILING_EDGE 0x02

volatile uint8_t _packet_counter = 0;
#define _packet_start_marker 0x01
#define _packet_end_marker 0x04

#define rx_buffer_size 255
uint8_t rx_buffer[rx_buffer_size];
uint8_t rx_idx = 0;
uint8_t rx_payload_size = 0;
uint8_t rx_payload_cmd = 0;
#define rx_max_payload_size 255
uint8_t rx_payload[rx_max_payload_size];

// For uploading the STM32 firmware
stm32_t *stm32=NULL;
uint32_t  stm32Addr=0;
char stm32FirmwareUpdMsg[256]={0x00};

void STM32reset()
{
  pinMode(STM_NRST_PIN, OUTPUT);

  pinMode(STM_BOOT0_PIN, OUTPUT);
  digitalWrite(STM_BOOT0_PIN, LOW); // boot stm from its own flash memory

  digitalWrite(STM_NRST_PIN, LOW); // start stm reset
  delay(50);
  digitalWrite(STM_NRST_PIN, HIGH); // end stm reset
  delay(50);
  sendCmdGetVersion();
}

void STM32ResetToDFUMode()
{
  logging::getLogStream().println("light: Request co-processor reset in dfu mode");

  pinMode(STM_NRST_PIN, OUTPUT);
  digitalWrite(STM_NRST_PIN, LOW);

  pinMode(STM_BOOT0_PIN, OUTPUT);
  digitalWrite(STM_BOOT0_PIN, HIGH);

  delay(50);

  // clear in the receive buffer
  while (Serial.available())
    Serial.read();

  digitalWrite(STM_NRST_PIN, HIGH); // pull out of reset
  delay(50); // wait 50ms fot the co-processor to come online
}


bool STM32FlashUpload(const uint8_t data[], unsigned int size)
{
  if (stm32==NULL)
    return false;
  unsigned int  len;
  uint8_t   buffer[256];
  const uint8_t *p_st = data;
  uint32_t end = stm32Addr + size;
  while (stm32Addr < end)
  {
    uint32_t left = end - stm32Addr;
    if (left<sizeof(buffer))
      len=left;
    else
      len=sizeof(buffer);

    memcpy(buffer, p_st, len);  // We need 4-byte bounadry flash access
    p_st += len;

    stm32_err_t s_err= stm32_write_memory(stm32, stm32Addr, buffer, len);
    if (s_err != STM32_ERR_OK)
    {
      // Error
      sprintf(stm32FirmwareUpdMsg,"failed to upload the STM32 firmware with error code %d",s_err);
      logging::getLogStream().printf("light: %s\n",stm32FirmwareUpdMsg);
      stm32=NULL;
      stm32Addr=0;
      return false;
    }
    else
      logging::getLogStream().printf("light: uploading firmware for STM32 with size %d\n",len);

    stm32Addr  += len;
  }
  return true;
}


bool STM32FlashBegin()
{
  Serial.end();
  Serial.begin(115200, SERIAL_8E1);
  STM32ResetToDFUMode();
  
  logging::getLogStream().println("light: start updating firmware for the STM32");

  stm32 = stm32_init(&Serial, STREAM_SERIAL, 1);
  stm32Addr = 0;
  if (stm32)
  {
    logging::getLogStream().println("light: STM32 erase memory");
    stm32_erase_memory(stm32, 0, STM32_MASS_ERASE);
    stm32Addr = stm32->dev->fl_start;
  }
  else
  {
    sprintf(stm32FirmwareUpdMsg,"failed to init the STM32 for uploading the firmware");
    logging::getLogStream().printf("light: %s\n",stm32FirmwareUpdMsg);
    return false;
  }
}

void STM32FlashEnd()
{
  logging::getLogStream().println("light: finish updating firmware for STM32");
  if (stm32)
    stm32_close(stm32);
  stm32=NULL;
  stm32Addr=0;
  Serial.end();
  STM32reset();
  Serial.begin(115200, SERIAL_8N1);
}



ICACHE_RAM_ATTR uint16_t crc(uint8_t *buffer, uint8_t len) {
  uint16_t c = 0;
  for (int i = 1; i < len; i++) {
    c += buffer[i];
  }
  return c;
}

ICACHE_RAM_ATTR void sendCommand(uint8_t cmd, uint8_t *payload, uint8_t len)
{
#define tx_buffer_size 255
  uint8_t tx_buffer[tx_buffer_size];
  uint8_t b = 0;

  tx_buffer[b++] = _packet_start_marker;
  tx_buffer[b++] = _packet_counter;
  tx_buffer[b++] = cmd;
  tx_buffer[b++] = len;

  if (payload) {
    memcpy(tx_buffer + b, payload, len);
  }
  b += len;

  uint16_t c = crc(tx_buffer, b);

  tx_buffer[b++] = c >> 8; // crc first byte (big/network endian)
  tx_buffer[b++] = c; // crc second byte (big/network endian)
  tx_buffer[b] = _packet_end_marker;

  b++;

  Serial.write(tx_buffer, b);
  //logging::getLogStream().printf("light: send packet %s\n", helpers::hexToStr(tx_buffer, b));

  _packet_counter++;

}

void processReceivedPacket(uint8_t payload_cmd, uint8_t* payload, uint8_t payload_size)
{
  logging::getLogStream().println("light: received packet");
  // Command for getting the version of the STM firmware
  if (payload_cmd == CMD_GET_VERSION)
  {
    logging::getLogStream().printf("light: STM Firmware version: %s\n", helpers::hexToStr(payload, payload_size));
    if (payload[0] != 0x3F || payload[1] != 0x02)
      logging::getLogStream().printf("light: STM Firmware is 0x%02X,0x%02X. It should be 0x3F,0x02\n", payload[0], payload[1]);

  }
  // Command for getting the state (brigthness level, wattage, etc)
  else if (payload_cmd == CMD_GET_STATE)
  {
    logging::getLogStream().printf("light: state: %s\n", helpers::hexToStr(payload, payload_size));
    // Bightness level: payload[3] payload[2]
    brightness = ((payload[3] << 8) + payload[2]) / 10;
    wattage = ((payload[7] << 8) + payload[6]) / 20;
    logging::getLogStream().printf("light: brightness level: %d%%\n", brightness);
    logging::getLogStream().printf("light: wattage level: %d watts\n", wattage);

    // To be done: other state values
    // See here:
    // https://github.com/arendst/Tasmota/issues/6914
    // https://github.com/KoljaWindeler/ESP8266_mqtt_pwm_pir_temp/blob/master/JKW_MQTT_PWM_PIR_TEMP/src/src/cap_shelly_dimmer.cpp#L49
    // https://github.com/KoljaWindeler/ESP8266_mqtt_pwm_pir_temp/blob/a3db486daedcd629183dabf4c0cd33842dca4f19/JKW_MQTT_PWM_PIR_TEMP/src/src/cap_shelly_dimmer.cpp#L108
    // https://github.com/wichers/shelly_dimmer/blob/master/shelly_dimmer.cpp#L286
    // https://gist.github.com/wichers/3c401f6dc6c61b4175ace7bc08cb6f8a -> 100% matching calibration algorithm

    /*
       Power 116 W
      at 75% -> 00 00 EE 02 00 00 78 09 00 00 00 80 00 00 00 00 -> 2424   -> Shelly firmware: 121 W
      at 50% -> 00 00 F4 01 00 00 E1 05 00 00 00 80 00 00 00 00 -> 1505   -> Shelly firmware: 77 W
      at 25% -> 00 00 FA 00 00 00 91 01 00 00 00 80 00 00 00 00 -> 401    -> Shelly firmware: 21 W

      Power 46 w
      at 75% -> 00 00 EE 02 00 00 C5 03 00 00 00 80 00 00 00 00 -> 965
      at 50% -> 00 00 F4 01 00 00 66 02 00 00 00 80 00 00 00 00 -> 614
      at 25% -> 00 00 FA 00 00 00 AB 00 00 00 00 80 00 00 00 00 -> 171

    */
  }
  else if (payload_cmd == CMD_SET_BRIGHTNESS)
    logging::getLogStream().printf("light: acknowledgement frame for changing brightness: %s\n", helpers::hexToStr(payload, payload_size));
  else if (payload_cmd == CMD_SET_BRIGHTNESS_ADVANCED)
    logging::getLogStream().printf("light: acknowledgement frame for changing brightness advanced: %s\n", helpers::hexToStr(payload, payload_size));
  else if (payload_cmd == CMD_SET_DIMMING_PARAMETERS)
    logging::getLogStream().printf("light: acknowledgement frame for changing dimming parameters: %s\n", helpers::hexToStr(payload, payload_size));
  else if (payload_cmd == CMD_SET_DIMMING_TYPE_2)
    logging::getLogStream().printf("light: acknowledgement frame for changing dimming 2: %s\n", helpers::hexToStr(payload, payload_size));
  else if (payload_cmd == CMD_SET_DIMMING_TYPE_3)
    logging::getLogStream().printf("light: acknowledgement frame for changing dimming 3: %s\n", helpers::hexToStr(payload, payload_size));
  else
    logging::getLogStream().printf("light: unknown command: 0x%02X\n", payload_cmd);
}

void receivePacket() {
  while (Serial.available() > 0)
  {
    uint8_t b = Serial.read();
    //logging::getLogStream().printf("rx_idx: %d  byte: %02X\n", rx_idx, b);
    rx_buffer[rx_idx] = b;

    if (rx_idx == 0 && b != _packet_start_marker) { // start marker
      logging::getLogStream().printf("light: received wrong start marker: 0x%02X\n", b);
      rx_idx = 0;
      continue;
    }

    if (rx_idx == rx_buffer_size - 1) {
      logging::getLogStream().println(F("light: rx buffer overflow"));
      rx_idx = 0;
      continue;
    }

    if (rx_idx == 1 && b != _packet_counter - 1) { // packet counter is same as previous tx packet
      logging::getLogStream().printf("light: packet counter seems to be wrong: 0x%02X\n", b);
      //rx_idx = 0;
      //continue;
    }

    if (rx_idx == 2) { // command
      rx_payload_cmd = b;
    }

    if (rx_idx == 3) { // payload size
      rx_payload_size = b;
      if (rx_payload_size > rx_max_payload_size)
        logging::getLogStream().printf("light: overflow with payload size %d\n", rx_payload_size);
    }

    if (rx_idx == (3 + rx_payload_size + 2)) { // checksum
      uint16_t c = (rx_buffer[rx_idx - 1] << 8) + b;
      if (c != crc(rx_buffer, rx_idx - 1)) {
        logging::getLogStream().printf("light: received wrong checksum at rx_idx: %d\n", rx_idx);
        rx_idx = 0;
        continue;
      }
      // Copy the payload
      if (rx_payload_size > 0)
      {
        memcpy(rx_payload, &rx_buffer[rx_idx - 1 - rx_payload_size], rx_payload_size);
      }
    }

    if (rx_idx == (3 + rx_payload_size + 3) && b != _packet_end_marker) { // end marker
      logging::getLogStream().printf("light: received wrong end marker: 0x%02X\n", b);
      rx_idx = 0;
      continue;
    }

    if (rx_idx == (3 + rx_payload_size + 3)) { // end marker
      //logging::getLogStream().println(F("light: received packet"));
      //helpers::hexToStr(rx_buffer, rx_idx + 1);
      // Process the packet which has just been received
      processReceivedPacket(rx_payload_cmd, rx_payload, rx_payload_size);
      rx_idx = 0;
      continue;
    }

    rx_idx++;
  }
}

ICACHE_RAM_ATTR void sendCmdSetBrightness(uint8_t b)
{
  //logging::getLogStream().printf("light: set brightness to %d‰\n", b);

  /*
    // see https://github.com/wichers/shelly_dimmer/blob/master/shelly_dimmer.cpp#L210
    uint8_t payload[] = {
    (uint8_t)(b * 1), (uint8_t)((b * 1) >> 8),             // b*10 second byte, b*10 first byte (little endian)
    0x00, 0x00,
    0x00, 0x00                                            // fade_rate
    };
    sendCommand(CMD_SET_BRIGHTNESS_ADVANCED, payload, sizeof(payload));*/
  uint8_t payload[] = { (uint8_t)(b * 10), (uint8_t)((b * 10) >> 8)};             // b*10 second byte, b*10 first byte (little endian)
  sendCommand(CMD_SET_BRIGHTNESS, payload, sizeof(payload));
}

void sendCmdSetDimmingParameters(uint8_t dimmingType, uint8_t debounce)
{
  logging::getLogStream().printf("light: change dimming type to %d and debounce value to %d\n", dimmingType, debounce);
  // Examples of frame
  //0x00, 0x00, 0x02, 0x00, 0x0f, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00};    // trailing edge
  //0x00, 0x00, 0x01, 0x00, 0x0f, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00      // leading edge
  //0x00, 0x00, 0x01, 0x00, 0x0F, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00      // debounce 150, leading edge
  //0x00, 0x00, 0x01, 0x00, 0x0F, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00      // debounce 50, leading edge
  //0x00, 0x00, 0x01, 0x00, 0x0F, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00      // debounce 100, leading edge
  //0x00, 0x00, 0x01, 0x00, 0x05, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00      // fade x1
  //0x00, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00      // fade x2
  //0x00, 0x00, 0x01, 0x00, 0x0F, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00      // fade x3
  //0x00, 0x00, 0x01, 0x00, 0x14, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00      // fade x4
  //          dim mode    fade rate    debounce
  uint8_t payload[] = {0x00, 0x00, 0x02, 0x00, 0x0f, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00};
  // Set the trailing/leading edge
  payload[2] = dimmingType;
  // Set the anti-flickering debounce parameter
  payload[6] = debounce;
  // Send the frame to change the dimming parameters
  sendCommand(CMD_SET_DIMMING_PARAMETERS, payload, sizeof(payload));
  // This is needed otherwise the STM32 stop working (most probably serial overflow)
  delay(12);
  receivePacket();

  // I do not knwo the use of this but this is needed for changing the dimming type trailinh/heading edge
  uint8_t payload2[0xC8] = {0x00};
  sendCommand(CMD_SET_DIMMING_TYPE_2, payload2, sizeof(payload2));
  delay(12);
  receivePacket();

  sendCommand(CMD_SET_DIMMING_TYPE_3, payload2, sizeof(payload2));
  delay(12);
  receivePacket();
}

void setBlinkingDuration(const char* durationStr)
{
  uint16_t newDuration=5;
  if (helpers::convertToInteger(durationStr, newDuration))
  {
    if (blinkingTimerDuration != newDuration)
    {
      logging::getLogStream().printf("light: change blinking duration to %d\n", newDuration);
      blinkingTimerDuration = newDuration;
    }
  }
  else
  {
    logging::getLogStream().printf("light: failed to change blinking duration to %s\n", durationStr);
    blinkingTimerDuration = 5;
  }

}

void startBlinking()
{
  logging::getLogStream().printf("light: start blinking\n");
  // start blinking
  blinkingLightState = false;              // light should be switched on
  lastBlinkingLightStateTime = millis();   // reset blinking timer
  startBlinkingTime = millis();            // Save the stating time of blinking
  blinking = true;
}

void stopBlinking()
{
  // stopping blinking
  // Comme back to the initial brightness level
  sendCmdSetBrightness(brightness);
  blinking = false;
}

void setBlinkingPattern(const char *payload)
{
  // payload should contain a sequence of int for the pattern of the blink (duration in ms for the light on, light off, etc)
  uint8_t nbPattern = 0;
  if (payload!=nullptr)
  {
    logging::getLogStream().printf("light: setting pattern to %s\n",payload);
    uint16_t strl = strlen(payload);
    memset(blinkingPattern, 0x00, sizeof(blinkingPattern));
    int i=0, j=0;
    char temp[10];
    memset(temp,0x00,sizeof(temp));
    for (i = 0; i <= strl; i++)
    {
      if (i == strl || payload[i]<'0' || payload[i]>'9')
      {
        if (helpers::isInteger(temp, sizeof(temp) - 1))
        {
          if (nbPattern < 10)
          {
            blinkingPattern[nbPattern] = atoi(temp)*100;
            logging::getLogStream().printf("light: adding pattern %d\n",blinkingPattern[nbPattern]);
            if (blinkingPattern[nbPattern]<200)
            {
              logging::getLogStream().printf("light: pattern duration to short. Set to 200\n");
              blinkingPattern[nbPattern]=200;
            }
            nbPattern++;
          }
        }
        j = 0;
        memset(temp,0x00,sizeof(temp));
      }
      else
      {
        if (j < sizeof(temp) - 1)
        {
          temp[j] = payload[i];
          j++;
        }
      }
    }
    logging::getLogStream().printf("light: new blinking pattern %d %d %d %d %d %d %d %d %d %d\n",
                                    blinkingPattern[0],blinkingPattern[1],blinkingPattern[2],blinkingPattern[3],blinkingPattern[4],
                                    blinkingPattern[5],blinkingPattern[6],blinkingPattern[7],blinkingPattern[8],blinkingPattern[9]);
  }
  if (nbPattern<2)
  {
    // If the blinking pattern is malformed (i.e. sequence smaller than 2)
    logging::getLogStream().printf("light: blinking pattern to short or not defined. Set back to default value\n");
    memset(blinkingPattern,0x00,sizeof(blinkingPattern));
    blinkingPattern[0]=500;
    blinkingPattern[1]=500;
  }
}

void mqttCallback(const char* paramID, const char* payload)
{
  if (strcmp(paramID, "subMqttLightOn") == 0 || strcmp(paramID, "subMqttLightAllOn") == 0)
  {
    lightOn();
  }
  else if (strcmp(paramID, "subMqttLightOff") == 0 || strcmp(paramID, "subMqttLightAllOff") == 0)
    lightOff();
  else if (strcmp(paramID, "subMqttBlinkingPattern") == 0)
  {
    setBlinkingPattern(payload);
    // Start blinking with the new pattern
    startBlinking();
  }
  else if (strcmp(paramID, "subMqttBlinkingDuration") == 0)
  {
    setBlinkingDuration(payload);
  }
}

void sendCmdGetVersion()
{
  sendCommand(CMD_GET_VERSION, 0, 0);
}

void sendCmdGetState()
{
  logging::getLogStream().printf("light: get state\n");
  sendCommand(CMD_GET_STATE, NULL, 0);
}

void setDimmingParameters(const char* dimmingTypeStr, const char* debounceStr)
{
  uint8_t dimmingType = TRAILING_EDGE, debounce = 100;
  if (helpers::isInteger(dimmingTypeStr, 1))
  {
    if (dimmingTypeStr[0] == '1')
      // leading edge
      dimmingType = LEADING_EDGE;
    else
      dimmingType = TRAILING_EDGE;
  }
  if (helpers::isInteger(debounceStr, 3))
  {
    debounce = atoi(debounceStr);
    // debounce value should be between 50 and 150
    if (debounce < 50)
      debounce = 50;
    if (debounce > 150)
      debounce = 150;
  }

  sendCmdSetDimmingParameters(dimmingType, debounce);
}

void setMinBrightness(const char* str)
{
  if (!helpers::isInteger(str, 3))
    return;

  // Make the conversion
  minBrightness = atoi (str);

  // Check if minBrightness between 0 and 200
  if (minBrightness < 0)
    minBrightness = 0;
  if (minBrightness > 20)
    minBrightness = 20;
}

void setMaxBrightness(const char* str)
{
  if (!helpers::isInteger(str, 3))
    return;

  // Make the conversion
  maxBrightness = atoi (str);

  // Check if maxBrightness between minBrightness and 1000
  if (maxBrightness < minBrightness)
    maxBrightness = minBrightness;
  if (maxBrightness > 100)
    maxBrightness = 100;
}

void setAutoOffTimer(const char* str)
{
  if (!helpers::isInteger(str, 3))
  {
    autoOffDuration = 0;
    return;
  }
  autoOffDuration = atoi (str);
}

void setBrightness(uint8_t b)
{
  sendCmdSetBrightness(b);
  brightness = b;
}

void lightOn(bool noLightAutoTurnOff)
{
  logging::getLogStream().printf("light: switch on\n");
  if (noLightAutoTurnOff==true)
  {
    lightAutoTurnOffDisable =true;
    lastLightOnTime=0;
  }
  else
    // Reset auto turn off timer
    lastLightOnTime=millis();
  setBrightness(maxBrightness);
  brightness = maxBrightness;
}

void lightOff()
{
  logging::getLogStream().printf("light: switch off\n");
  lastLightOnTime = 0;
  lightAutoTurnOffDisable =false;
  setBrightness(minBrightness);
  brightness = minBrightness;
}

ICACHE_RAM_ATTR void lightToggle(bool noLightAutoTurnOff)
{
  // If brighness different from minBrightness
  if (brightness == minBrightness)
  {
    if (noLightAutoTurnOff==true)
    {
      lightAutoTurnOffDisable =true;
      lastLightOnTime=0;
    }
    else
      // Reset auto turn off timer
      lastLightOnTime=millis();

    // Switch on the light
    sendCmdSetBrightness(maxBrightness);
    brightness = maxBrightness;
  }
  else
  {
    // Set the timer to 0
    lastLightOnTime = 0;
    lightAutoTurnOffDisable =false;
    // Switch off te light
    sendCmdSetBrightness(minBrightness);
    brightness = minBrightness;
  }
}

ICACHE_RAM_ATTR bool lightIsOn()
{
  return brightness != minBrightness;
}

void setup()
{
  STM32reset();
}

void updateParams()
{
  logging::getLogStream().printf("light: updateParams\n");
  setMinBrightness(wifi::getParamValueFromID("minBrightness"));
  setMaxBrightness(wifi::getParamValueFromID("maxBrightness"));
  setAutoOffTimer(wifi::getParamValueFromID("autoOffTimer"));
}

  
void handleUploadSTM32Firmware()
{
  char temp[700];

  snprintf ( temp, 700,
             "<!DOCTYPE html>\
              <html>\
                <head>\
                  <title>STM32 Firmware update</title>\
                  <meta charset=\"utf-8\">\
                  <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\
                  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
                </head>\
                <body>\
                  <form action=\"/doUploadSTM32Firmware\" method=\"post\" enctype=\"multipart/form-data\">\
                    <input type=\"file\" name=\"data\">\
                    <button>Update STM32 Firmware</button>\
                   </form>\
                </body>\
              </html>");
  wifi::getWifiManager().server.get()->send ( 200, "text/html", temp );
}


void handleDoUploadSTM32Firmware()
{
  if (wifi::getWifiManager().server.get()->uri() != "/doUploadSTM32Firmware")
    return;
  HTTPUpload& upload = wifi::getWifiManager().server.get()->upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    logging::getLogStream().printf("wifi: start uploading STM32 firmware\n");
    memset(stm32FirmwareUpdMsg,0x00,sizeof(stm32FirmwareUpdMsg));
    STM32FlashBegin();
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    STM32FlashUpload(upload.buf, upload.currentSize);
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    STM32FlashEnd();
    // If not message, the firmware update has succeeded
    if (strlen(stm32FirmwareUpdMsg)==0)
      sprintf(stm32FirmwareUpdMsg,"STM32 firmware update succeeded");
  }

}

void addWifiManagerCustomButtons()
{
  for (int i = 0; i < sizeof(wifiManagerCustomButtons) / sizeof(WiFiManagerParameter); i++)
    wifi::getWifiManager().addParameter(&wifiManagerCustomButtons[i]);
}

void addWifiManagerCustomParams()
{
  for (int i = 0; i < sizeof(wifiManagerCustomParams) / sizeof(WiFiManagerParameter); i++)
    wifi::getWifiManager().addParameter(&wifiManagerCustomParams[i]);
}

// HTTP callback for updating the STM32 firmware
void bindServerCallback()
{
  // Handle to upload the configuration file
  wifi::getWifiManager().server.get()->on("/uploadSTM32Firmware", HTTP_GET, handleUploadSTM32Firmware);
  // Upload file
  // - first callback is called after the request has ended with all parsed arguments
  // - second callback handles file upload at that location
  wifi::getWifiManager().server.get()->on("/doUploadSTM32Firmware", HTTP_POST, []()
                                          {
                                            wifi::getWifiManager().server.get()->send(200, "text/plain", stm32FirmwareUpdMsg); 
                                          },
                                          handleDoUploadSTM32Firmware);
}

void handle()
{
  unsigned long currTime;

  // Process packets from the STM32 MCU
  receivePacket();

  // Check if there is new brightness value to publish
  if (publishedBrightness != brightness)
  {
    // Publish the new value of the brightness
    const char* topic = wifi::getParamValueFromID("pubMqttBrightnessLevel");
    // If no topic, we do not publish
    if (topic != NULL)
    {
      char payload[5];
      sprintf(payload, "%d", brightness);
      if (mqtt::publishMQTT(topic, payload))
        // If the new brightness value has been succeefully published
        publishedBrightness = brightness;
    }
  }

  // For blinking
  if (blinking)
  {
    currTime = millis();

    // Check if the blinking has to be stopped
    if (currTime - startBlinkingTime > blinkingTimerDuration*1000)
    {
      logging::getLogStream().printf("light: stop blinking\n");
      stopBlinking();
    }

    // Alternate on/off for the blinking
    if (blinking)
    {
      // Using the pattern, find the new light state
      long diff = currTime - lastBlinkingLightStateTime;
      uint8_t pc = 0;
      unsigned long sum = 0;
      while (diff > sum)
      {
        // If at the end of the pattern, we come back
        if (pc == 10)
        {
          lastBlinkingLightStateTime = lastBlinkingLightStateTime + sum;
          diff = currTime - lastBlinkingLightStateTime;
          logging::getLogStream().printf("light: loop over the pattern\n");
          pc = 0;
          sum = 0;
        }
        sum += blinkingPattern[pc];
        pc++;
      }
      
      bool newBlinkingLightState = ((pc-1)%2==0);
      if (blinkingLightState != newBlinkingLightState)
      {
        blinkingLightState = newBlinkingLightState;
        // alternate on/off
        if (blinkingLightState)
        {
          logging::getLogStream().printf("light: light on for blinking\n");
          sendCmdSetBrightness(maxBrightness);
        }
        else
        {
          logging::getLogStream().printf("light: light off for blinking\n");
          sendCmdSetBrightness(minBrightness);
        }
      }
    }
  }

  // For the auto-off light
  if (autoOffDuration > 0 && lastLightOnTime > 0)
  {
    currTime = millis();
    // Make the conversion from ms to s
    if (currTime - lastLightOnTime > (autoOffDuration * 1000))
    {
      logging::getLogStream().printf("light: auto-off light\n");
      lightOff();
    }
  }
}

} // namespace dimmer
