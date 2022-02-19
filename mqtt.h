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
  bool publishMQTT(const char *topic, const char *payload);
}

#endif
