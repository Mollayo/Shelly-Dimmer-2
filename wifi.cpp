#include <ESP8266WiFi.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "wifi.h"
#include "logging.h"
#include "config.h"
#include "mqtt.h"


namespace wifi {


// For the firmware update
ESP8266HTTPUpdateServer httpUpdater;
// The webserver for wifi and MQTT configuration
WiFiManager wifiManager(logging::getLogStream());
WiFiManager &getWifiManager(){return wifiManager;}



// Custom parameters for the MQTT
WiFiManagerParameter customParamInit[] = {
  // Button for the firmware update
  WiFiManagerParameter("</form><form action=\"/update\"><input type=\"submit\" value=\"Update firmware\"></form><form action=\"/paramsave\">"),
  // The switch parameters
  WiFiManagerParameter("<br/><br/><hr><h3>Switch parameters</h3>"),
  WiFiManagerParameter("switchType", "Switch type (1: push button, 2: toggle button)", "2", 1),
  WiFiManagerParameter("defaultReleaseState", "Default release state (0: open, 1: close)", "0", 1),

  // The dimmer parameters
  WiFiManagerParameter("<br/><br/><hr><h3>Dimmer parameters</h3>"),
  WiFiManagerParameter("minBrighness", "Minimum brightness (0‰ to 200‰)", "0", 4),
  WiFiManagerParameter("maxBrighness", "Maximum brightness (0‰ to 1000‰)", "500", 4),
  WiFiManagerParameter("dimmingType", "Dimming type (0: trailing edge, 1: leading edge)", "2", 1),
  WiFiManagerParameter("flickerDebounce", "Anti-flickering debounce (50 - 150)", "100", 3),

  // The MQTT server parameters
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT server</h3>"),
  WiFiManagerParameter("mqttServer", "IP of the broker", "", 40),
  WiFiManagerParameter("mqttPort", "Port", "1883", 6),

  // The MQTT publish
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT publish</h3>"),
  WiFiManagerParameter("pubMqttBrighnessLevel", "Brightness change", "brighness", 100),
  WiFiManagerParameter("pubMqttSwitchEvents", "Switch events", "switchEvent", 100),
  WiFiManagerParameter("pubMqttOverheat", "Overheat alarm", "overheat", 100),

  // The MQTT subscribe
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT subscribe</h3>"),
  WiFiManagerParameter("subMqttLightOn", "Topic for switching on", "switchOn", 100),
  WiFiManagerParameter("subMqttLightAllOn", "Topic for switching on all lights", "switchOnAll", 100),
  WiFiManagerParameter("subMqttLightOff", "Topic for switching off", "switchOff", 100),
  WiFiManagerParameter("subMqttLightAllOff", "Topic for switching off all lights", "switchOffAll", 100),
  WiFiManagerParameter("subMqttStartBlink", "Topic for starting blink", "startBlink", 100),
  WiFiManagerParameter("subMqttStopLight", "Topic for stopping blink", "stopBlink", 100),
  WiFiManagerParameter("subMqttStartFastBlink", "Topic for starting fast blink", "startFastBlink", 100),
  WiFiManagerParameter("subMqttStopFastBlink", "Topic for stopping fast blink", "stopFastBlink", 100),

  // The debugging options
  WiFiManagerParameter("<br/><br/><hr><h3>Debugging options</h3>"),
  WiFiManagerParameter("logOutput", "Logging (0: disable, 1: to Serial, 2: to Telnet, 3: to a file)", "0", 1),
  WiFiManagerParameter("<a href=\"/log.txt\">Open_the_log_file</a>&emsp;<a href=\"/erase_log_file\">Erase_the_log_file</a><br/><br/>"),
};



void handle()
{
  // Handle for the config portal
  wifiManager.process();
}

// Convert param ID to param value
const char* getParamValueFromID(const char* str)
{
  WiFiManagerParameter** customParams = wifiManager.getParameters();
  for (int i = 0; i < wifiManager.getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strncmp(customParams[i]->getID(), str, strlen(str)) == 0)
    {
      if (customParams[i]->getValue() == NULL)
        return NULL;
      if (strlen(customParams[i]->getValue()) == 0)
        return NULL;
      return customParams[i]->getValue();
    }
  }
  return NULL;
}

// Convert param value to param ID
const char* getIDFromParamValue(const char* str)
{
  WiFiManagerParameter** customParams = wifiManager.getParameters();
  for (int i = 0; i < wifiManager.getParametersCount(); i++)
  {
    if (customParams[i]->getValue() == NULL)
      continue;
    if (strncmp(customParams[i]->getValue(), str, strlen(str)) == 0)
    {
      if (customParams[i]->getID() == NULL)
        return NULL;
      if (strlen(customParams[i]->getID()) == 0)
        return NULL;
      return customParams[i]->getID();
    }
  }
  return NULL;
}

int getIndexFromID(const char* str)
{
  WiFiManagerParameter** customParams = wifiManager.getParameters();
  for (int i = 0; i < wifiManager.getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strncmp(customParams[i]->getID(), str, strlen(str)) == 0)
    {
      if (customParams[i]->getValue() == NULL)
        return -1;
      if (strlen(customParams[i]->getValue()) == 0)
        return -1;
      return i;
    }
  }
  return -1;
}

// Update the system with the new params
void updateSystemWithWifiManagerParams()
{
  // Update the configuration settings for logging -> should be done first
  logging::configure();

  // Update the configuration settings for MQTT
  mqtt::configure();

  // Update the configuration settings for the switches
  switches::configure();

    // Update the configuration settings for the dimmer
  dimmer::configure();
}

// callback to save the custom params
void saveParams()
{
  logging::getLogStream().println("wifi: saving custom parameters");

  // Create the json object from the custom parameters
  DynamicJsonDocument jsonBuffer(1024);
  WiFiManagerParameter** customParams = wifiManager.getParameters();
  for (int i = 0; i < wifiManager.getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strlen(customParams[i]->getID()) == 0)
      continue;
    if (customParams[i]->getValue() == NULL)
      continue;
    logging::getLogStream().printf("wifi: found custom param to save: \"%s\" with value \"%s\"\n",customParams[i]->getID(),customParams[i]->getValue());
    jsonBuffer[customParams[i]->getID()] = customParams[i]->getValue();
  }


  // Open the file
  if (SPIFFS.begin())
  {
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      SPIFFS.end();
      logging::getLogStream().println("wifi: failed to open config.json");
      return;
    }
    // Save the json object to the file
    serializeJson(jsonBuffer, configFile);
    // Close the file
    configFile.close();
    SPIFFS.end();
    logging::getLogStream().printf("wifi: saving: ");
    serializeJson(jsonBuffer, logging::getLogStream());
    logging::getLogStream().println();
  }
  else
    logging::getLogStream().println("wifi: failed to mounted file system");

  // Update the system with the new params
  updateSystemWithWifiManagerParams();
}


// callback to load the custom params
void loadParams()
{
  logging::getLogStream().println("wifi: loading custom parameters");
  if (SPIFFS.begin())
  {
    if (SPIFFS.exists("/config.json"))
    {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);

        // Close file
        configFile.close();
        SPIFFS.end();

        // Process the json data
        DynamicJsonDocument jsonBuffer(1024);
        DeserializationError error = deserializeJson(jsonBuffer, buf.get());
        if (!error)
        {
          WiFiManagerParameter** customParams = wifiManager.getParameters();
          logging::getLogStream().print("wifi: json to load: ");
          serializeJson(jsonBuffer, logging::getLogStream());
          logging::getLogStream().println();
          JsonObject root = jsonBuffer.as<JsonObject>();
          for (JsonObject::iterator it=root.begin(); it!=root.end(); ++it)
          {
            int idx=getIndexFromID(it->key().c_str());
            if (idx!=-1)
            {
              logging::getLogStream().printf("wifi: reading key \"%s\" and value \"%s\"\n", it->key().c_str(), it->value().as<char*>());
              customParams[idx]->setValue(it->value().as<char*>(), customParams[idx]->getValueLength());
            }
            else
              logging::getLogStream().printf("wifi: key \"%s\" with value \"%s\" not found\n", it->key().c_str(), it->value().as<char*>());
          }
        }
        else
          logging::getLogStream().println("wifi: failed to load json params");
      }
      else
      {
        SPIFFS.end();
        logging::getLogStream().println("wifi: failed to open config.json file");
      }
    }
    else
    {
      SPIFFS.end();
      logging::getLogStream().println("wifi: no config.json file");
    }
  }
  else
    logging::getLogStream().println("wifi: failed to mounted file system");
}


void bindServerCallback()
{
  // This is to handle the web page for updating the firmware
  httpUpdater.setup(wifiManager.server.get(), "/update");
  // Handle for managing the log file on SPIFFS
  wifiManager.server.get()->on("/log.txt", logging::displayLogFile);
  wifiManager.server.get()->on("/erase_log_file", logging::eraseLogFile);
}

void setup()
{
  //wifiManager.resetSettings();              // Reset the wifi settings for debugging

  // Add the custom parameters
  for (int i = 0; i < sizeof(customParamInit) / sizeof(WiFiManagerParameter); i++)
    wifiManager.addParameter(&customParamInit[i]);

  // Load the custom parameters
  loadParams();
  updateSystemWithWifiManagerParams();

  logging::getLogStream().println("wifi: starting WiFi...");

  // for rebooting in 2 minutes if still in AP mode
  wifiManager.setConfigPortalTimeout(120);

  // The menu options on the main page
  const char* menu[] = {"wifi", "info", "param", "restart"};
  wifiManager.setMenu(menu, 4);

  // Set the callback for saving the custom parameters
  wifiManager.setSaveParamsCallback(saveParams);

  // Blocking at the access point
  wifiManager.setConfigPortalBlocking(true);
  String ssid = "SHELLY_DIMMER_2";          // SSID for the access point

  if (wifiManager.autoConnect(ssid.c_str()))
  {
    //if you get here you have connected to the WiFi
    logging::getLogStream().println("wifi: connected to wifi network!");
    WiFi.mode(WIFI_STA);
    wifiManager.setWebServerCallback(bindServerCallback);     // Add the callback to handle the page for updating the firmware
    wifiManager.startWebPortal();                             // Start the web server of WifiManager
  }
  else
  {
    // If it goes here, still access point, will reboot
    logging::getLogStream().println("wifi: still in AP mode; reboot now");
    wifiManager.reboot();
    //ESP.restart();
  }
}



}
