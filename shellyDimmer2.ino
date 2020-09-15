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
  digitalWrite(SHELLY_BUILTIN_LED, HIGH);  // LED off

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
  char* telnetCmd=logging::handleTelnet();
  if (telnetCmd!=NULL)
  {
    // 's' to send the "get state" command
    if (telnetCmd[0] == 's')
      dimmer::sendCmdGetState();
    else if (telnetCmd[0] == 'b')
    {
      // '0' to '9' to set the brightness from 0% to 90%
      uint16_t v = (telnetCmd[1] - '0') * 1000 + (telnetCmd[2] - '0') * 100 + (telnetCmd[3] - '0') * 10 + (telnetCmd[4] - '0');
      if (v>=0 && v<=1000)
        dimmer::sendCmdBrightness(v);
      else
        logging::getLogStream().printf("wrong value for the brightness: %d\n",v);
    }
    else if (telnetCmd[0] == 'v')
      dimmer::sendCmdVersion();
    else if (telnetCmd[0] == 'o' && telnetCmd[1] == 'n')
      dimmer::switchOn();
    else if (telnetCmd[0] == 'o' && telnetCmd[1] == 'f' && telnetCmd[2] == 'f')
      dimmer::switchOff();
    else
      // Command not recognized, we print the menu options
      logging::printTelnetMenu();
  }

  // Process data for MQTT
  mqtt::handle();
  
  // Process serial data from the MCU
  dimmer::handle();

  // Process the switches events
  switches::handle();

}
