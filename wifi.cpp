#include <ESP8266WiFi.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

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

// Parameters for the firmware and configuration file
WiFiManagerParameter customButtons[] = 
{
  // Button for the firmware update
  WiFiManagerParameter("</form>"),
  WiFiManagerParameter(version),
  WiFiManagerParameter("<form action=\"/config.json\"><input type=\"submit\" value=\"Download the configuration file\"></form>"),
  WiFiManagerParameter("<form action=\"/config_upload\"><input type=\"submit\" value=\"Upload the configuration file\"></form>")
};
  
// The switch parameters
WiFiManagerParameter switchParams[] = 
{
  WiFiManagerParameter("<form action=\"/paramsave\">"),
  WiFiManagerParameter("<br/><br/><hr><h3>Switch parameters</h3>"),
  WiFiManagerParameter("hostname", "Hostname and access point name (require reboot)", "", 30),
  WiFiManagerParameter("switchType", "Switch type (1: push button, 2: toggle button)", "2", 2),
  WiFiManagerParameter("defaultReleaseState", "Switch state for light off (0: open, 1: close(less prone to noise))", "0", 2),
  WiFiManagerParameter("autoOffTimer", "Auto-off timer (value in seconds). Auto-off is disable for long push button press.", "", 3),
};

// The MQTT server parameters
WiFiManagerParameter MQTTParams[] = 
{
  // The broker parameters
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT server</h3>"),
  WiFiManagerParameter("mqttServer", "IP of the broker", "", 40),
  WiFiManagerParameter("mqttPort", "Port", "1883", 6),

  // The MQTT publish
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT publish</h3>"),
  WiFiManagerParameter("pubMqttBrightnessLevel", "Brightness change", "light/shellyDevice", 100),
  WiFiManagerParameter("pubMqttSwitchEvents", "Switch events", "switch/shellyDevice", 100),
  WiFiManagerParameter("pubMqttAlarmOverheat", "Overheat alarm", "shellyDevice/alarm/overheat", 100),
  WiFiManagerParameter("pubMqttTemperature", "Internal temperature", "temperature/shellyDevice", 100),

  // The MQTT subscribe
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT subscribe</h3>"),
  WiFiManagerParameter("subMqttLightOn", "Topic for switching on", "switchOn/shellyDevice", 100),
  WiFiManagerParameter("subMqttLightAllOn", "Topic for switching on all lights", "switchOnAll", 100),
  WiFiManagerParameter("subMqttLightOff", "Topic for switching off", "switchOff/shellyDevice", 100),
  WiFiManagerParameter("subMqttLightToggle", "Topic for light toggling", "toggle/shellyDevice", 100),  
  WiFiManagerParameter("subMqttLightAllOff", "Topic for switching off all lights", "switchOffAll", 100),
  WiFiManagerParameter("subMqttBlinkingPattern", "Topic for starting blinking with the pattern given in the MQTT message. \
                                                  The pattern is optional. It is specified with a sequence of integers indicating \
                                                  the duration of the on/off states. The durations are in tenths of seconds.", "startBlinking", 100),
  WiFiManagerParameter("subMqttBlinkingDuration", "Topic for changing the blinking duration in seconds", "setBlinkingDuration", 100),
};

// The debugging options
WiFiManagerParameter loggingParams[] = 
{
  WiFiManagerParameter("<br/><br/><hr><h3>Logging options</h3>"),
  WiFiManagerParameter("logOutput", "Logging (0: disable, 1: to Serial, 2: to Telnet, 3: to the log file)", "0", 2),
  WiFiManagerParameter("<a href=\"/log.txt\">Open_the_log_file</a>&emsp;<a href=\"/erase_log_file\">Erase_the_log_file</a><br/><br/>"),
};

void handle()
{
  // Handle for the config portal
  wifiManager.process();

  // Update the built-in led to show the wifi connection status
  // Try to reconnect every minute if not connected
  unsigned long now = millis();
  if(WiFi.status() != WL_CONNECTED)
    // builtin led slowly blinking when not connected to the wifi
    switches::enableBuiltinLedBlinking(switches::LED_SLOW_BLINKING);
  else
    switches::enableBuiltinLedBlinking(switches::LED_ON);
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
  //logging::getLogStream().println("wifi: saving custom parameters");

  WiFiManagerParameter** customParams = wifiManager.getParameters();

  // Open the file
  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile)
  {
    logging::getLogStream().println("wifi: failed to open config.json");
    return;
  }
  configFile.print("{\n");
  bool printComma=false;
  for (int i = 0; i < wifiManager.getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strlen(customParams[i]->getID()) == 0)
      continue;
    if (customParams[i]->getValue() == NULL)
      continue;
    if (printComma)
    {
      configFile.print(",\n");
      printComma=false;
    }
    char tmp[256];
    sprintf(tmp,"\"%s\":\"%s\"",customParams[i]->getID(),customParams[i]->getValue());
    configFile.print(tmp);
    // SPIFFS: There seems to be some conflicting interaction between the 3.0.0 core and the deprecated SPIFFS
    // see: https://github.com/esp8266/Arduino/issues/8070
    //logging::getLogStream().printf("wifi: writing %s\n", tmp);
    printComma=true;
  }
  configFile.print("\n}");

  // Close the file
  configFile.close();
  //logging::getLogStream().printf("wifi: saving: ");
  //logging::getLogStream().println();

  // Update the system with the new params
  updateSystemWithWifiManagerParams();
}


// callback to load the custom params
void loadParams()
{
  logging::getLogStream().println("wifi: loading custom parameters");
  if (LittleFS.exists("/config.json"))
  {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile)
    {
      // Process the json data
      DynamicJsonDocument jsonBuffer(2048);
      DeserializationError error = deserializeJson(jsonBuffer, configFile);
      if (!error)
      {
        WiFiManagerParameter** customParams = wifiManager.getParameters();
        // Should not be too verbose otherwise it triggers the watchdog reset
        //logging::getLogStream().print("wifi: json to load: ");
        //serializeJson(jsonBuffer, logging::getLogStream());
        //logging::getLogStream().println();
        JsonObject root = jsonBuffer.as<JsonObject>();
        for (JsonObject::iterator it = root.begin(); it != root.end(); ++it)
        {
          int idx = getIndexFromID(it->key().c_str());
          if (idx != -1)
          {
            // Should not be too verbose otherwise it triggers the watchdog reset
            //logging::getLogStream().printf("wifi: reading key \"%s\" and value \"%s\"\n", it->key().c_str(), it->value().as<char*>());
            customParams[idx]->setValue(it->value().as<char*>(), customParams[idx]->getValueLength());
          }
          else
            logging::getLogStream().printf("wifi: key \"%s\" with value \"%s\" not found\n", it->key().c_str(), it->value().as<char*>());
        }
      }
      else
        logging::getLogStream().println("wifi: failed to load json params");
      // Close file
      configFile.close();
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

void handlePrepareConfigFileUpload()
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
        <form action=\"/config_upload\" method=\"post\" enctype=\"multipart/form-data\">\
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
  if (wifiManager.server.get()->uri() != "/config_upload")
    return;
  HTTPUpload& upload = wifiManager.server.get()->upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    logging::getLogStream().printf("wifi: start uploading with the LittleFS name \"/config.json\"\n");
    fsUploadFile = LittleFS.open("/config.json", "w");
    if (!fsUploadFile)
      logging::getLogStream().printf("wifi: failed with LittleFS.open()\n");
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
  if (LittleFS.exists(wifi::getWifiManager().server.get()->uri()))
  {
    File file = LittleFS.open(wifi::getWifiManager().server.get()->uri(), "r");
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
  else
    logging::getLogStream().printf("wifi: no file with name %s\n",wifi::getWifiManager().server.get()->uri().c_str());
  // In case of error log file
  wifi::getWifiManager().server.get()->send(200, "text/plain", "No file");
}

void handleSeverPathNotFound()
{
  wifiManager.server.get()->send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
  logging::getLogStream().printf("wifi: access to %s\n",wifiManager.server.get()->uri().c_str());
  if (wifiManager.server.get()->args()>0)
  {
    logging::getLogStream().printf("wifi: with %d arguments\n",wifiManager.server.get()->args());
    // Show the arguments
    for (int i = 0; i < wifiManager.server.get()->args(); i++) 
    {
      logging::getLogStream().printf("     - %s -> ",wifiManager.server.get()->argName(i).c_str());
      logging::getLogStream().printf("%s\n",wifiManager.server.get()->arg(i).c_str());
    }
  }
}

void bindServerCallback()
{
  // Handle for managing the log file on LittleFS
  wifiManager.server.get()->on("/log.txt", handleFileDownload);
  wifiManager.server.get()->on("/erase_log_file", logging::eraseLogFile);

  // Handle to backup the configuration file
  wifiManager.server.get()->on("/config.json", handleFileDownload);
  
  // Handle to upload the configuration file
  wifiManager.server.get()->on("/config_upload", HTTP_GET, handlePrepareConfigFileUpload);
  wifiManager.server.get()->on("/config_upload", HTTP_POST, []() {
                               wifiManager.server.get()->send(200, "text/plain", "Finished uploading the configuration file");
                             }, handleConfigFileUpload);

  // callbacks for updating the STM32 firmware
  light::bindServerCallback();
}

void factoryReset()
{
  logging::getLogStream().println("wifi: factory reset and reboot...");
  wifiManager.erase(true);
  if (LittleFS.format())
    logging::getLogStream().println("wifi: failed to format LittleFS");
  else
    logging::getLogStream().println("wifi: LittleFS erased");
  // LittleFS should be unmounted in order to effectivly erae the all the files
  LittleFS.end();
  wifiManager.reboot();
}

void prepareOTA()
{
  // Disable timer interrupt since it can corrupt the OTA update
  switches::disableInterrupt(),
  // Disable the serial connection since it can also corrupt the OTA update
  Serial.end();
}

void setup()
{
  //wifiManager.resetSettings();              // Reset the wifi settings for debugging

  // Add the custom parameters
  for (int i = 0; i < sizeof(customButtons) / sizeof(WiFiManagerParameter); i++)
    wifiManager.addParameter(&customButtons[i]);
  light::addWifiManagerCustomButtons();
  
  for (int i = 0; i < sizeof(switchParams) / sizeof(WiFiManagerParameter); i++)
    wifiManager.addParameter(&switchParams[i]);

  light::addWifiManagerCustomParams();

  for (int i = 0; i < sizeof(MQTTParams) / sizeof(WiFiManagerParameter); i++)
    wifiManager.addParameter(&MQTTParams[i]);

  for (int i = 0; i < sizeof(loggingParams) / sizeof(WiFiManagerParameter); i++)
    wifiManager.addParameter(&loggingParams[i]);

  wifiManager.setPreOtaUpdateCallback(prepareOTA);

  // Load the custom parameters
  loadParams();
  updateSystemWithWifiManagerParams();

  logging::getLogStream().println("wifi: starting WiFi...");

  // The menu options on the main page
  const char* menu[] = {"wifi", "info", "param", "update", "restart"};
  wifiManager.setMenu(menu, 5);

  // Set the callback for saving the custom parameters
  wifiManager.setSaveParamsCallback(saveParams);

  // Non blocking at the access point
  wifiManager.setConfigPortalBlocking(false);
  // Add the callbacks to handle the pages for setting the parameters and updating the firmware
  wifiManager.setWebServerCallback(bindServerCallback);
  
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
    logging::handle();


    // After 1 minute in access point with no client connected and Wifi SSID and password defined, reboot automatically to try connecting again
    if ((WiFi.SSID()!=nullptr) && (WiFi.softAPgetStationNum()==0) && (millis() - startAPTime > 60000))
    {
      logging::getLogStream().println("wifi: still in AP mode; reboot now");
      wifiManager.reboot();
    }
  }

  // if you get here you have connected to the WiFi
  logging::getLogStream().println("wifi: connected to wifi network!");
  // Set station mode
  WiFi.mode(WIFI_STA);
  wifiManager.startWebPortal();                             // Start the web server of WifiManager
  // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"
  // Should be done after startWebPortal()
  wifiManager.server.get()->onNotFound(handleSeverPathNotFound);

  // Setup for MQTT
  mqtt::setup();
}



}
