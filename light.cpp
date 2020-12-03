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
uint8_t prevBrightness = 0;
uint8_t publishedBrightness = 0;      // The last brigthness value published to MQTT
uint8_t wattage = 0;

// For the auto-off timer
uint16_t autoOffDuration = 0;         // In seconds
unsigned long lastLightOnTime = 0;

// For blinking
unsigned long lastBlinkingTime = 0;
uint16_t blinkingDuration = 0;   // in ms; if 0, no blinking
bool blinkingLightState = false;


uint8_t &getWattage() {
  return wattage;
}

WiFiManagerParameter wifiManagerCustomButtons[] = 
{
  // Button for the firmware update
  WiFiManagerParameter("<form action=\"/uploadSTM32Firmware\"><input type=\"submit\" value=\"Update the STM32 Firmware\"></form>"),
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
      logging::getLogStream().println("light: error uploading firmware for STM32");
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
    logging::getLogStream().println("light: failed to init the STM32 for uploading the firmware");
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
    logging::getLogStream().printf("- STM Firmware version: %s\n", helpers::hexToStr(payload, payload_size));
    if (payload[0] != 0x3F || payload[1] != 0x02)
      logging::getLogStream().printf("- STM Firmware is 0x%02X,0x%02X. It should be 0x3F,0x02\n", payload[0], payload[1]);

  }
  // Command for getting the state (brigthness level, wattage, etc)
  else if (payload_cmd == CMD_GET_STATE)
  {
    logging::getLogStream().printf("- state: %s\n", helpers::hexToStr(payload, payload_size));
    // Bightness level: payload[3] payload[2]
    brightness = ((payload[3] << 8) + payload[2]) / 10;
    wattage = ((payload[7] << 8) + payload[6]) / 20;
    logging::getLogStream().printf("- brightness level: %d%%\n", brightness);
    logging::getLogStream().printf("- wattage level: %d watts\n", wattage);

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
    logging::getLogStream().printf("- acknowledgement frame for changing brightness: %s\n", helpers::hexToStr(payload, payload_size));
  else if (payload_cmd == CMD_SET_BRIGHTNESS_ADVANCED)
    logging::getLogStream().printf("- acknowledgement frame for changing brightness advanced: %s\n", helpers::hexToStr(payload, payload_size));
  else if (payload_cmd == CMD_SET_DIMMING_PARAMETERS)
    logging::getLogStream().printf("- acknowledgement frame for changing dimming parameters: %s\n", helpers::hexToStr(payload, payload_size));
  else if (payload_cmd == CMD_SET_DIMMING_TYPE_2)
    logging::getLogStream().printf("- acknowledgement frame for changing dimming 2: %s\n", helpers::hexToStr(payload, payload_size));
  else if (payload_cmd == CMD_SET_DIMMING_TYPE_3)
    logging::getLogStream().printf("- acknowledgement frame for changing dimming 3: %s\n", helpers::hexToStr(payload, payload_size));
  else
    logging::getLogStream().printf("- unknown command: 0x%02X\n", payload_cmd);
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

void mqttCallback(const char* paramID, const char* payload)
{
  if (strcmp(paramID, "subMqttLightOn") == 0 || strcmp(paramID, "subMqttLightAllOn") == 0)
  {
    // reset the timer
    lastLightOnTime  = millis();
    lightOn();
  }
  else if (strcmp(paramID, "subMqttLightOff") == 0 || strcmp(paramID, "subMqttLightAllOff") == 0)
    lightOff();
  else if (strcmp(paramID, "subMqttStartBlink") == 0)
    setBlinkingDuration(1000);
  else if (strcmp(paramID, "subMqttStartFastBlink") == 0)
    setBlinkingDuration(500);
  else if (strcmp(paramID, "subMqttStopBlink") == 0)
    setBlinkingDuration(0);
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

void lightOn()
{
  logging::getLogStream().printf("light: switch on\n");
  setBrightness(maxBrightness);
}

void lightOff()
{
  logging::getLogStream().printf("light: switch off\n");
  setBrightness(minBrightness);
}

ICACHE_RAM_ATTR void lightToggle()
{
  // Current brighness closer to minBrightness than maxBrighness
  if ((brightness - minBrightness) < (maxBrightness - brightness))
  {
    sendCmdSetBrightness(maxBrightness);
    brightness = maxBrightness;
  }
  else
  {
    sendCmdSetBrightness(minBrightness);
    brightness = minBrightness;
  }
}

ICACHE_RAM_ATTR bool lightIsOn()
{
  return brightness > minBrightness;
}

void setBlinkingDuration(uint16_t duration)
{
  if (blinkingDuration != duration)
  {
    logging::getLogStream().printf("light: change blinking duration to %d\n", duration);
    blinkingDuration = duration;
    if (duration == 0)
    {
      // stopping blinking
      // Comme back to the initial brightness level
      sendCmdSetBrightness(brightness);
    }
    else
    {
      // start blinking
      blinkingLightState = true;  // start with light on
      lastBlinkingTime = 0;       // reset blinking timer
    }
  }
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

  setDimmingParameters(wifi::getParamValueFromID("dimmingType"), wifi::getParamValueFromID("flickerDebounce"));
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
    STM32FlashBegin();
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    STM32FlashUpload(upload.buf, upload.currentSize);
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    STM32FlashEnd();
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
  wifi::getWifiManager().server.get()->on("/doUploadSTM32Firmware", HTTP_POST, [](){ wifi::getWifiManager().server.get()->send(200, "text/plain", ""); }, handleDoUploadSTM32Firmware);
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

  // For blinking,  blinkingDuration==0 -> no blinking
  if (blinkingDuration > 0)
  {
    currTime = millis();
    if (currTime - lastBlinkingTime > blinkingDuration)
    {
      lastBlinkingTime = currTime;
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
      blinkingLightState = !blinkingLightState;
    }
  }

  // For the auto-off light
  if (brightness != prevBrightness)
  {
    // Set the auto-off timer for the brightness change
    if (brightness > minBrightness)
      lastLightOnTime  = millis();
    else
      lastLightOnTime = 0;
    prevBrightness = brightness;
  }
  if (autoOffDuration > 0 && lastLightOnTime > 0)
  {
    currTime = millis();
    // Make the conversion from ms to s
    if (currTime - lastLightOnTime > (autoOffDuration * 1000))
    {
      logging::getLogStream().printf("light: auto-off light\n");
      lightOff();
      lastLightOnTime = 0;
    }
  }
}

} // namespace dimmer
