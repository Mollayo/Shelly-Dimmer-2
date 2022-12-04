
/*
  Firmware for Shelly Dimmer 2: https://kb.shelly.cloud/knowledge-base/shelly-dimmer-2
*/


#include "wifi.h"
#include "logging.h"
#include "mqtt.h"
#include "light.h"
#include "switches.h"

#include "LittleFS.h"

void setup()
{
  // Setup serial
  Serial.begin(115200);

  // Mount the LittleFS
  if (!LittleFS.begin())
    logging::getLogStream().println("Failed to mounted file system");

  // Setup for the switches and the light
  switches::setup();
  // Initialise the STM32 MCU
  light::setup();
  // Fast blinking to show that the device is booting
  switches::enableBuiltinLedBlinking(switches::LED_FAST_BLINKING);

  // Setup telnet for logging and debugging, should be first
  wifi::setup();

  // LED on to show that the device is ready
  switches::enableBuiltinLedBlinking(switches::LED_ON);
}


// the loop function runs over and over again forever
void loop()
{
  wifi::handle();

  // Process the telnet commands
  // Interactive console for debugging
  logging::handle();

  // Process data for MQTT
  mqtt::handle();

  // Process serial data from the MCU
  light::handle();

  // Process the switches events
  switches::handle();
}
