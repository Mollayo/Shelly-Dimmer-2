/*
  Firmware for Shelly Dimmer 2
*/


#include "wifi.h"
#include "logging.h"
#include "mqtt.h"
#include "dimmer.h"
#include "switches.h"



void setup()
{
  // For the built-in LED
  pinMode(SHELLY_BUILTIN_LED, OUTPUT);
  digitalWrite(SHELLY_BUILTIN_LED, HIGH);

  // Setup serial
  Serial.begin(115200);
  // wait for serial port to connect. Needed for Native USB only
  while (!Serial) {
    ;
  }
  Serial.flush();

  // Setup telnet for logging and debugging, should be first
  wifi::setup();

  // Setup for MQTT
  mqtt::setup();

  // Setup for the switches
  switches::setup();

  // Setup for the MCU
  dimmer::setup();

  // Switch on the built-in LED to show that it is now running the main loop
  digitalWrite(SHELLY_BUILTIN_LED, LOW);  // LED on
}


// the loop function runs over and over again forever
void loop()
{
  wifi::handle();
  
  // Process the telnet commands
  // Interactive console for debugging and analysing the serial communation with the STM MCU
  logging::handle();
  
  // Process data for MQTT
  mqtt::handle();
  
  // Process serial data from the MCU
  dimmer::handle();

  // Process the switches events
  switches::handle();

}
