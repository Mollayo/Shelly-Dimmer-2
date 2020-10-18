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
WiFiManager &getWifiManager() {
  return wifiManager;
}

const char version[] = "Build Date & Time: " __DATE__ ", " __TIME__;

// Custom parameters for the MQTT
WiFiManagerParameter customParamInit[] = {
  // Button for the firmware update
  WiFiManagerParameter("</form>"),
  WiFiManagerParameter(version),
  WiFiManagerParameter("<form action=\"/update\"><input type=\"submit\" value=\"Update firmware\"></form>"),
  WiFiManagerParameter("<form action=\"/config.json\"><input type=\"submit\" value=\"Download the configuration file\"></form>"),
  WiFiManagerParameter("<form action=\"/upload\"><input type=\"submit\" value=\"Upload the configuration file\"></form>"),
  WiFiManagerParameter("<form action=\"/paramsave\">"),
  // The switch parameters
  WiFiManagerParameter("<br/><br/><hr><h3>Switch parameters</h3>"),
  WiFiManagerParameter("hostname", "Hostname and access point name (require reboot)", "", 30),
  WiFiManagerParameter("switchType", "Switch type (1: push button, 2: toggle button)", "2", 1),
  WiFiManagerParameter("defaultReleaseState", "Default release state (0: open, 1: close)", "0", 1),

  // The dimmer parameters
  WiFiManagerParameter("<br/><br/><hr><h3>Light parameters</h3>"),
  WiFiManagerParameter("minBrightness", "Minimum brightness (0% to 20%)", "0", 3),
  WiFiManagerParameter("maxBrightness", "Maximum brightness (0% to 100%)", "50", 3),
  WiFiManagerParameter("autoOffTimer", "Auto-off timer (value in seconds)", "", 3),
  WiFiManagerParameter("dimmingType", "Dimming type (0: trailing edge (LED), 1: leading edge (halogen))", "0", 1),
  WiFiManagerParameter("flickerDebounce", "Anti-flickering debounce (50 - 150)", "100", 3),

  // The MQTT server parameters
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT server</h3>"),
  WiFiManagerParameter("mqttServer", "IP of the broker", "", 40),
  WiFiManagerParameter("mqttPort", "Port", "1883", 6),

  // The MQTT publish
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT publish</h3>"),
  WiFiManagerParameter("pubMqttBrightnessLevel", "Brightness change", "light/shellyDevice", 100),
  WiFiManagerParameter("pubMqttSwitchEvents", "Switch events", "switch/shellyDevice", 100),
  WiFiManagerParameter("pubMqttOverheat", "Overheat alarm", "overheat/shellyDevice", 100),
  WiFiManagerParameter("pubMqttTemperature", "Internal temperature", "temperature/shellyDevice", 100),
  WiFiManagerParameter("pubMqttConnecting", "Connecting to the broker", "connecting/shellyDevice", 100),

  // The MQTT subscribe
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT subscribe</h3>"),
  WiFiManagerParameter("subMqttLightOn", "Topic for switching on", "switchOn/shellyDevice", 100),
  WiFiManagerParameter("subMqttLightAllOn", "Topic for switching on all lights", "switchOnAll", 100),
  WiFiManagerParameter("subMqttLightOff", "Topic for switching off", "switchOff/shellyDevice", 100),
  WiFiManagerParameter("subMqttLightAllOff", "Topic for switching off all lights", "switchOffAll", 100),
  WiFiManagerParameter("subMqttStartBlink", "Topic for starting blinking", "startBlink/shellyDevice", 100),
  WiFiManagerParameter("subMqttStartFastBlink", "Topic for starting fast blinking", "startFastBlink/shellyDevice", 100),
  WiFiManagerParameter("subMqttStopBlink", "Topic for stopping blinking", "stopBlink/shellyDevice", 100),

  // The debugging options
  WiFiManagerParameter("<br/><br/><hr><h3>Debugging options</h3>"),
  WiFiManagerParameter("logOutput", "Logging (0: disable, 1: to Serial, 2: to Telnet, 3: to a file)", "2", 1),
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
    if (strcmp(customParams[i]->getID(), str) == 0)
      return i;
  }
  return -1;
}

// Update the system with the new params
void updateSystemWithWifiManagerParams()
{
  // Update the configuration for the wifiManager
  const char* hn = getParamValueFromID("hostname");
  if (hn != NULL && strlen(hn) > 0)
    wifiManager.setHostname(hn);

  // Update the configuration settings for logging -> should be done first
  logging::updateParams();

  // Update the configuration settings for MQTT
  mqtt::updateParams();

  // Update the configuration settings for the switches
  switches::updateParams();

  // Update the configuration settings for the dimmer
  light::updateParams();
}

// callback to save the custom params
void saveParams()
{
  logging::getLogStream().println("wifi: saving custom parameters");

  // Create the json object from the custom parameters
  DynamicJsonDocument jsonBuffer(2048);
  WiFiManagerParameter** customParams = wifiManager.getParameters();
  for (int i = 0; i < wifiManager.getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strlen(customParams[i]->getID()) == 0)
      continue;
    if (customParams[i]->getValue() == NULL)
      continue;
    logging::getLogStream().printf("wifi: found custom param to save: \"%s\" with value \"%s\"\n", customParams[i]->getID(), customParams[i]->getValue());
    jsonBuffer[customParams[i]->getID()] = customParams[i]->getValue();
  }


  // Open the file
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    logging::getLogStream().println("wifi: failed to open config.json");
    return;
  }
  // Save the json object to the file
  serializeJson(jsonBuffer, configFile);
  // Close the file
  configFile.close();
  logging::getLogStream().printf("wifi: saving: ");
  serializeJson(jsonBuffer, logging::getLogStream());
  logging::getLogStream().println();

  // Update the system with the new params
  updateSystemWithWifiManagerParams();
}


// callback to load the custom params
void loadParams()
{
  logging::getLogStream().println("wifi: loading custom parameters");
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

      // Process the json data
      DynamicJsonDocument jsonBuffer(2048);
      DeserializationError error = deserializeJson(jsonBuffer, buf.get());
      if (!error)
      {
        WiFiManagerParameter** customParams = wifiManager.getParameters();
        logging::getLogStream().print("wifi: json to load: ");
        serializeJson(jsonBuffer, logging::getLogStream());
        logging::getLogStream().println();
        JsonObject root = jsonBuffer.as<JsonObject>();
        for (JsonObject::iterator it = root.begin(); it != root.end(); ++it)
        {
          int idx = getIndexFromID(it->key().c_str());
          if (idx != -1)
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
      logging::getLogStream().println("wifi: failed to open config.json file");
    }
  }
  else
  {
    logging::getLogStream().println("wifi: no config.json file");
  }
}

void handleUpload()
{
  char temp[700];

  snprintf ( temp, 700,
             "<!DOCTYPE html>\
    <html>\
      <head>\
        <title>ESP8266 Upload</title>\
        <meta charset=\"utf-8\">\
        <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
      </head>\
      <body>\
        <form action=\"/edit\" method=\"post\" enctype=\"multipart/form-data\">\
          <input type=\"file\" name=\"data\">\
          <button>Upload</button>\
         </form>\
      </body>\
    </html>"
           );
  wifiManager.server.get()->send ( 200, "text/html", temp );
}

void handleConfigFileUpload()
{
  static File fsUploadFile;
  if (wifiManager.server.get()->uri() != "/edit")
    return;
  HTTPUpload& upload = wifiManager.server.get()->upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    logging::getLogStream().printf("wifi: start uploading with the SPIFFS name \"/config.json\"\n");
    fsUploadFile = SPIFFS.open("/config.json", "w");
    if (!fsUploadFile)
      logging::getLogStream().printf("wifi: failed with SPIFFS.open()\n");
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (fsUploadFile)
    {
      fsUploadFile.write(upload.buf, upload.currentSize);
      logging::getLogStream().printf("wifi: handleFileUpload data: %d\n", upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (fsUploadFile)
    {
      fsUploadFile.close();
      logging::getLogStream().printf("wifi: handleFileUpload size: %d\n", upload.totalSize);
      loadParams();
      updateSystemWithWifiManagerParams();
    }
  }
}

void handleFileDownload()
{
  if (SPIFFS.exists(wifi::getWifiManager().server.get()->uri()))
  {
    File file = SPIFFS.open(wifi::getWifiManager().server.get()->uri(), "r");
    if (file)
    {
      size_t fileSize = file.size();
      const uint8_t bufSize = 20;
      char buf[bufSize];
      //now send an empty string but with the header field Content-Length to let the browser know how much data it should expect
      wifi::getWifiManager().server.get()->setContentLength(fileSize);
      wifi::getWifiManager().server.get()->send(200, "application/x-binary", "");

      int sentSize = 0;
      while (sentSize < fileSize)
      {
        uint8_t sizeToSend = bufSize;
        if (sentSize + bufSize >= fileSize)
          sizeToSend = fileSize - sentSize;

        file.readBytes(buf, sizeToSend);
        wifi::getWifiManager().server.get()->sendContent(buf, sizeToSend);

        sentSize = sentSize + sizeToSend;
      }
      file.close();
      return;
    }
  }
  // In case of error log file
  wifi::getWifiManager().server.get()->send(200, "text/plain", "No file");
}

void bindServerCallback()
{
  // This is to handle the web page for updating the firmware
  httpUpdater.setup(wifiManager.server.get(), "/update");
  // Handle for managing the log file on SPIFFS
  wifiManager.server.get()->on("/log.txt", handleFileDownload);
  wifiManager.server.get()->on("/erase_log_file", logging::eraseLogFile);

  // Handle to backup and upload the configuration file
  wifiManager.server.get()->on("/upload", handleUpload);
  wifiManager.server.get()->on("/config.json", handleFileDownload);

  // Upload file
  // - first callback is called after the request has ended with all parsed arguments
  // - second callback handles file upload at that location
  wifiManager.server.get()->on("/edit", HTTP_POST, [](){ wifiManager.server.get()->send(200, "text/plain", ""); }, handleConfigFileUpload);
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

  // The menu options on the main page
  const char* menu[] = {"wifi", "info", "param", "restart"};
  wifiManager.setMenu(menu, 4);

  // Set the callback for saving the custom parameters
  wifiManager.setSaveParamsCallback(saveParams);

  // Non blocking at the access point
  wifiManager.setConfigPortalBlocking(false);

  // SSID for the access point
  const char* hn = getParamValueFromID("hostname");
  if (hn != NULL && strlen(hn) > 0)
    wifiManager.autoConnect(hn);
  else
  {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    wifiManager.autoConnect(helpers::hexToStr(mac, 6));
  }

  // Slow blinking to show the AP mode
  switches::enableBuiltinLedBlinking(switches::LED_SLOW_BLINKING);

  // AP mode is enabled
  unsigned long startAPTime = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    // Process user interaction in AP mode
    wifiManager.process();
    // This is to make the light and switch working in AP mode
    light::handle();
    switches::handle();

    // After 1 minute in access point, reboot automatically
    if (millis() - startAPTime > 60000)
    {
      logging::getLogStream().println("wifi: still in AP mode; reboot now");
      wifiManager.reboot();
      //ESP.restart();
    }
  }

  //if you get here you have connected to the WiFi
  logging::getLogStream().println("wifi: connected to wifi network!");
  WiFi.mode(WIFI_STA);
  wifiManager.setWebServerCallback(bindServerCallback);     // Add the callback to handle the page for updating the firmware
  wifiManager.startWebPortal();                             // Start the web server of WifiManager

  // Setup for MQTT
  mqtt::setup();
}



}
