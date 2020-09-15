#include <Arduino.h>

#include "mqtt.h"
#include "wifi.h"
#include "logging.h"
#include "config.h"
#include "dimmer.h"
#include "switches.h"



namespace dimmer {
const uint8_t CMD_SET_BRIGHTNESS = 0x03;
const uint8_t CMD_GET_STATE = 0x10;
const uint8_t CMD_GET_VERSION = 0x01;
const uint8_t CMD_SET_DIMMING_TYPE_1 = 0x20;
const uint8_t CMD_SET_DIMMING_TYPE_2 = 0x30;
const uint8_t CMD_SET_DIMMING_TYPE_3 = 0x31;

uint8_t _packet_counter = 0;
uint8_t _packet_start_marker = 0x01;
uint8_t _packet_end_marker = 0x04;

const uint8_t buffer_size = 255;
char tx_buffer[buffer_size];
char rx_buffer[buffer_size];
uint8_t rx_idx = 0;
uint8_t rx_payload_size = 0;
uint8_t rx_payload_cmd = 0;
const uint8_t rx_max_payload_size = 255;
char rx_payload[rx_max_payload_size];


// The light parameters
uint16_t minBrightness = 50;   // brightness values in ‰
uint16_t maxBrightness = 500;
uint16_t brightness = 0;
uint8 wattage = 0;


uint16_t &getMinBrightness() {
  return minBrightness;
}
uint16_t &getMaxBrightness() {
  return maxBrightness;
}
uint16_t &getBrightness() {
  return brightness;
}
uint8 &getWattage() {
  return wattage;
}


uint16_t crc(char *buffer, uint8_t len) {
  uint16_t c = 0;
  for (int i = 1; i < len; i++) {
    c += buffer[i];
  }
  return c;
}

void processReceivedPacket(uint8_t payload_cmd, char* payload, uint8_t payload_size)
{
  logging::getLogStream().println("dimmer: received packet");
  // Command for getting the version of the STM firmware
  if (payload_cmd == CMD_GET_VERSION)
  {
    logging::getLogStream().printf("- STM Firmware version: 0x%02X,0x%02X\n", payload[0], payload[1]);
    if (payload[0] !=0x35 || payload[1]!=0x02)
      logging::getLogStream().printf("- STM Firmware should be: 0x%02X,0x%02X\n", 0x35, 0x02);
    
  }
  // Command for getting the state (brigthness level, wattage, etc)
  else if (payload_cmd == CMD_GET_STATE)
  {
    logging::getLogStream().printf("- state: %s\n", helpers::hexToStr(payload, payload_size));
    // Bightness level: payload[3] payload[2]
    brightness = ((payload[3] << 8) + payload[2]) / 1;
    wattage = ((payload[7] << 8) + payload[6]) / 20;
    logging::getLogStream().printf("- brighness level: %d‰\n", brightness);
    logging::getLogStream().printf("- wattage level: %d\n", wattage);

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
  else if (payload_cmd == CMD_SET_DIMMING_TYPE_1)
    logging::getLogStream().printf("- acknowledgement frame for changing dimming 1: %s\n", helpers::hexToStr(payload, payload_size));
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
      logging::getLogStream().printf("dimmer: received wrong start marker: 0x%02X\n", b);
      rx_idx = 0;
      continue;
    }

    if (rx_idx == buffer_size - 1) {
      logging::getLogStream().println(F("dimmer: rx buffer overflow"));
      rx_idx = 0;
      continue;
    }

    if (rx_idx == 1 && b != _packet_counter - 1) { // packet counter is same as previous tx packet
      logging::getLogStream().printf("dimmer: packet counter seems to be wrong: 0x%02X\n", b);
      //rx_idx = 0;
      //continue;
    }

    if (rx_idx == 2) { // command
      rx_payload_cmd = b;
    }

    if (rx_idx == 3) { // payload size
      rx_payload_size = b;
      if (rx_payload_size > rx_max_payload_size)
        logging::getLogStream().printf("dimmer: overflow with payload size %d\n", rx_payload_size);
    }

    if (rx_idx == (3 + rx_payload_size + 2)) { // checksum
      uint16_t c = (rx_buffer[rx_idx - 1] << 8) + b;
      if (c != crc(rx_buffer, rx_idx - 1)) {
        logging::getLogStream().printf("dimmer: received wrong checksum at rx_idx: %d\n", rx_idx);
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
      logging::getLogStream().printf("dimmer: received wrong end marker: 0x%02X\n", b);
      rx_idx = 0;
      continue;
    }

    if (rx_idx == (3 + rx_payload_size + 3)) { // end marker
      //logging::getLogStream().println(F("dimmer: received packet"));
      //helpers::hexToStr(rx_buffer, rx_idx + 1);
      // Process the packet which has just been received
      processReceivedPacket(rx_payload_cmd, rx_payload, rx_payload_size);
      rx_idx = 0;
      continue;
    }

    rx_idx++;
  }
}

void sendCommand(uint8_t cmd, uint8_t *payload, uint8_t len) {
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
  logging::getLogStream().printf("dimmer: send packet %s\n", helpers::hexToStr(tx_buffer, b));

  _packet_counter++;

  // An acknowledgement packet should always be received from the STM
  delay(12);
  receivePacket();
}

void sendCmdVersion() {
  sendCommand(CMD_GET_VERSION, 0, 0);
}

void sendCmdBrightness(uint16_t b) {
  // Publish the messange to MQTT
  if (brightness != b)
  {
    brightness = b;
    mqtt::publishMQTTChangeBrightness(b);
  }

  logging::getLogStream().printf("dimmer: set brightness to %d‰\n", b);

  // see https://github.com/wichers/shelly_dimmer/blob/master/shelly_dimmer.cpp#L210
  uint8_t payload[] = {
    (uint8_t)(b * 1), (uint8_t)((b * 1) >> 8),             // b*10 second byte, b*10 first byte (little endian)
    (uint8_t)(b * 8 / 10), (uint8_t)((b * 8 / 10) >> 8),
    0x00, 0x00                                            // fade_rate
  };
  sendCommand(CMD_SET_BRIGHTNESS, payload, sizeof(payload));
}

void sendCmdGetState() {
  logging::getLogStream().printf("dimmer: get state\n");
  sendCommand(CMD_GET_STATE, NULL, 0);
}

void sendCmdSetTrailingEdge()
{
  logging::getLogStream().printf("dimmer: set trailing edge\n");
  // This requires sending 3 frames
  uint8_t payload1[] = {0x00, 0x00, 0x02, 0x00, 0x0f, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00};
  sendCommand(CMD_SET_DIMMING_TYPE_1, payload1, sizeof(payload1));

  uint8_t payload2[0xC8] = {0x00};
  sendCommand(CMD_SET_DIMMING_TYPE_2, payload2, sizeof(payload2));

  sendCommand(CMD_SET_DIMMING_TYPE_3, payload2, sizeof(payload2));
}

void sendCmdSetLeadingEdge()
{
  logging::getLogStream().printf("dimmer: set leading edge\n");
  // This requires sending 3 frames
  uint8_t payload1[] = {0x00, 0x00, 0x01, 0x00, 0x0f, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00};
  sendCommand(CMD_SET_DIMMING_TYPE_1, payload1, sizeof(payload1));

  uint8_t payload2[0xC8] = {0x00};
  sendCommand(CMD_SET_DIMMING_TYPE_2, payload2, sizeof(payload2));

  sendCommand(CMD_SET_DIMMING_TYPE_3, payload2, sizeof(payload2));
}

void setDimmingType(const char* str)
{
  if (!helpers::isInteger(str, 1))
    return;
  if (str[0] == '1')
  {
    // leading edge
    dimmer::sendCmdSetLeadingEdge();
  }
  else
  {
    // Trailing edge (default option)
    dimmer::sendCmdSetTrailingEdge();
  }

}

void setMinBrightness(const char* str)
{
  if (!helpers::isInteger(str, 4))
    return;

  // Make the conversion
  minBrightness = atoi (str);

  // Check if minBrightness between 0 and 200
  if (minBrightness < 0)
    minBrightness = 0;
  if (minBrightness > 200)
    minBrightness = 200;
}

void setMaxBrightness(const char* str)
{
  if (!helpers::isInteger(str, 4))
    return;

  // Make the conversion
  maxBrightness = atoi (str);

  // Check if maxBrightness between minBrightness+100 and 1000
  if (maxBrightness < minBrightness + 100)
    maxBrightness = minBrightness + 100;
  if (maxBrightness > 1000)
    maxBrightness = 1000;
}

void switchOn()
{
  // LED: maximum 200
  // At the booting stage, the light should be switched on/off at 100
  dimmer::receivePacket();
  dimmer::sendCmdBrightness(getMaxBrightness());
}

void switchOff()
{
  dimmer::sendCmdBrightness(getMinBrightness());
}

void switchToggle()
{
  // Current brighness closer to minBrighness than maxBrighness
  if ((getBrightness() - getMinBrightness()) < (getMaxBrightness() - getBrightness()))
    switchOn();
  else
    switchOff();
}

void setup() {
  pinMode(STM_NRST_PIN, OUTPUT);

  // Does not seems to be needed
  pinMode(STM_BOOT0_PIN, OUTPUT);
  digitalWrite(STM_BOOT0_PIN, LOW); // boot stm from its own flash memory

  digitalWrite(STM_NRST_PIN, LOW); // start stm reset
  delay(50);
  digitalWrite(STM_NRST_PIN, HIGH); // end stm reset
  delay(50);
  sendCmdVersion();
}

void configure()
{
  setMinBrightness(wifi::getParamValueFromID("minBrighness"));
  setMaxBrightness(wifi::getParamValueFromID("maxBrighness"));
  setDimmingType(wifi::getParamValueFromID("dimmingType"));
}

void handle() {
  receivePacket();
}

} // namespace dimmer
