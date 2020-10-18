#ifndef MQTT
#define MQTT

#include "config.h"
#include "wifi.h"
#include "switches.h"

namespace mqtt
{ 
  void callback(char* topic, byte* payload, unsigned int length);
  void updateParams();
  void setup();
  boolean reconnect();
  void handle();

  // Methods for publishing to MQTT
  void publishMQTTOverheating(int temperature);
  bool publishMQTT(const char *topic, const char *payload, int QoS=1);
}
  

#endif
