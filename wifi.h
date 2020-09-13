#ifndef WIFI
#define WIFI

#include <ESP8266WiFi.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

///////////////////////////////
// A class to handle logging //
///////////////////////////////


namespace wifi {

  WiFiManager &getWifiManager();
  
  
  void handle();
  const char* getParamValueFromID(const char* str);
  const char* getIDFromParamValue(const char* str);
  void updateSystemWithWifiManagerParams();
  void saveParams();
  void loadParams();
  void bindServerCallback();
  void setup();

}

#endif
