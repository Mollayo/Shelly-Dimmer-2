#ifndef MQTT
#define MQTT

#include "config.h"
#include "wifi.h"
#include "switches.h"
#include <PubSubClient.h>

namespace mqtt
{ 
  void callback(char* topic, byte* payload, unsigned int length);
  void configure();
  void setup();
  boolean reconnect();
  void handle();

  // Methodes for publishing to MQTT
  void publishMQTTChangeBrightness(uint8 brightnessLevel);
  void publishMQTTChangeSwitch(uint8 swID, uint swState);
  void publishMQTTOverheating(int temperature);
}
  

#endif
