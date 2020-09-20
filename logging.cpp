#include "logging.h"

#include <ESP8266WiFi.h>
#include "config.h"
#include "wifi.h"
#include "dimmer.h"
#include "switches.h"

namespace logging
{

// Telent server for logging and debugging
WiFiServer *TelnetServer = NULL;    // (23)
WiFiClient Telnet;
char *telnetCmd = NULL;

//////////////////////////////////////////////////////////////////////
// A class to handle logging                                        //
// Logging can be disabled or it can be to Serial, Telent or a File //
//////////////////////////////////////////////////////////////////////
LogStream::LogStream()
{
  logOutput = LogStream::LogDisabled;
  //logOutput = LogStream::LogToFile;
  //logOutput = LogStream::LogToSerial;
}

void LogStream::setLogOutput(const char *c)
{
  if (helpers::isInteger(c, 1))
  {
    if (c[0] == '1')
      logOutput = LogToSerial;
    else if (c[0] == '2')
      logOutput = LogToTelnet;
    else if (c[0] == '3')
      logOutput = LogToFile;
    else
      logOutput = LogDisabled;
  }
  else
    // Default option
    logOutput = LogDisabled;
}

size_t LogStream::write(uint8_t data)
{
  size_t tmp = 0;
  switch (logOutput)
  {
    case LogToSerial:
      return Serial.write(data);
    case LogToTelnet:
      return Telnet.write(data);
    case LogToFile:
      if (SPIFFS.begin())
      {
        File logFile = SPIFFS.open("/log.txt", "a");
        if (logFile)
        {
          tmp = logFile.write(data);
          logFile.close();
        }
        SPIFFS.end();
      }
      return tmp;
    default:
      return 0;
  }
}
int LogStream::availableForWrite()
{
  switch (logOutput)
  {
    case LogToSerial:
      return Serial.availableForWrite();
    case LogToTelnet:
      return Telnet.availableForWrite();
  }
  return 0;
}
int LogStream::available()
{
  // No reading from the stream
  return 0;
}
int LogStream::read()
{
  // No reading from the stream
  return 0;
}
int LogStream::peek()
{
  // No reading from the stream
  return 0;
}
void LogStream::flush()
{
  switch (logOutput)
  {
    case LogToSerial:
      Serial.flush();
    case LogToTelnet:
      Telnet.flush();
  }
}

LogStream logStream;
LogStream &getLogStream() {
  return logStream;
}


//////////////////////
// Telnet functions //
//////////////////////
char* readTelnetCmd()
{
  if (!TelnetServer)
    return NULL;

  // Handle for the telnet
  if (TelnetServer->hasClient())
  {
    // client is connected
    if (!(Telnet) || !Telnet.connected())
    {
      if (Telnet)
        Telnet.stop();         // client disconnected
      Telnet = TelnetServer->available(); // ready for new client
    }
    else
    {
      TelnetServer->available().stop();  // have client, block new connections
    }
  }

  const uint8_t MAXBUFFERSIZE = 30;
  if (Telnet && Telnet.connected() && Telnet.available())
  {
    char c;
    uint8_t charsReceived = 0;
    if (telnetCmd == NULL)
      telnetCmd = (char*)calloc(sizeof(char), MAXBUFFERSIZE);

    // copy waiting characters into textBuff
    //until textBuff full, CR received, or no more characters
    while (Telnet.available() && charsReceived < MAXBUFFERSIZE && c != 0x0d)
    {
      c = Telnet.read();
      telnetCmd[charsReceived] = c;
      charsReceived++;
    }
    // Skip new line feed
    if (charsReceived == 1 && telnetCmd[0] == 10)
      return NULL;
    if (charsReceived > 0)
    {
      if (charsReceived < MAXBUFFERSIZE)
        telnetCmd[charsReceived]=0x00;
      return telnetCmd;
    }
  }
  return NULL;
}

void printTelnetMenu()
{
  // Print the telnet menu
  if (Telnet)
  {
    Telnet.println("Commands:");
    Telnet.println(" s : get the state of the MCU");
    Telnet.println(" v : get the version of the MCU");
    Telnet.println(" br0000 to br1000 : set the brighness between 0‰ and 1000‰");
    Telnet.println(" on or off : switch on/off the light");
    Telnet.println(" temp : enable/disable temperature logging");
    Telnet.println(" bl0000 to bl9999 : set blinking duration");
  }
}

void handle()
{
  char* telnetCmd = readTelnetCmd();
  if (telnetCmd != NULL)
  {
    // 's' to send the "get state" command
    if (telnetCmd[0] == 's' && telnetCmd[1] == 0x0D)
      dimmer::sendCmdGetState();
    else if (telnetCmd[0] == 'b' && telnetCmd[1] == 'r' && telnetCmd[6] == 0x0D)
    {
      // '0' to '9' to set the brightness from 0% to 90%
      uint16_t v = (telnetCmd[2] - '0') * 1000 + (telnetCmd[3] - '0') * 100 + (telnetCmd[4] - '0') * 10 + (telnetCmd[5] - '0');
      if (v >= 0 && v <= 1000)
        dimmer::setBrightness(v);
      else
        logging::getLogStream().printf("wrong value for the brightness: %d\n", v);
    }
    else if (telnetCmd[0] == 'v' && telnetCmd[1] == 0x0D)
      dimmer::sendCmdGetVersion();
    else if (telnetCmd[0] == 'o' && telnetCmd[1] == 'n' && telnetCmd[2] == 0x0D)
      dimmer::switchOn();
    else if (telnetCmd[0] == 'o' && telnetCmd[1] == 'f' && telnetCmd[2] == 'f' && telnetCmd[3] == 0x0D)
      dimmer::switchOff();
    else if (telnetCmd[0] == 't' && telnetCmd[1] == 'e'&& telnetCmd[2] == 'm' && telnetCmd[3] == 'p' && telnetCmd[4] == 0x0D)
      switches::getTemperatureLogging()=!switches::getTemperatureLogging();
    else if (telnetCmd[0] == 'b' && telnetCmd[1] == 'l' && telnetCmd[6] == 0x0D)
    {
      uint16_t v = (telnetCmd[2] - '0') * 1000 + (telnetCmd[3] - '0') * 100 + (telnetCmd[4] - '0') * 10 + (telnetCmd[5] - '0');
      if (v >= 0 && v <= 1000)
        dimmer::setBlinkingDuration(v);
      else
        logging::getLogStream().printf("wrong value for the blink duration: %d\n", v);
    }
    else
      // Command not recognized, we print the menu options
      printTelnetMenu();
  }
}

void enableTelnet()
{
  if (TelnetServer == NULL)
  {
    logStream.println("Starting telnet server");
    TelnetServer = new WiFiServer(23);
    TelnetServer->begin();
  }
}

void disableTelnet()
{
  if (TelnetServer)
  {
    logStream.println("Stopping telnet server");
    if (Telnet)
      Telnet.stop();         // client disconnected
    TelnetServer->close();
    TelnetServer->stop();

    delete TelnetServer;
    TelnetServer = NULL;
    if (telnetCmd)
    {
      free(telnetCmd);
      telnetCmd = NULL;
    }
  }
}


void displayFile(const String &fileName)
{
  if (SPIFFS.begin())
  {
    if (SPIFFS.exists(fileName))
    {
      File logFile = SPIFFS.open(fileName, "r");
      if (logFile)
      {
        size_t fileSize = logFile.size();
        const uint8 bufSize = 20;
        char buf[bufSize];
        //now send an empty string but with the header field Content-Length to let the browser know how much data it should expect
        wifi::getWifiManager().server.get()->setContentLength(fileSize);
        wifi::getWifiManager().server.get()->send(200, "application/x-binary", "");

        int sentSize = 0;
        while (sentSize < fileSize)
        {
          uint8 sizeToSend = bufSize;
          if (sentSize + bufSize >= fileSize)
            sizeToSend = fileSize - sentSize;

          logFile.readBytes(buf, sizeToSend);
          wifi::getWifiManager().server.get()->sendContent(buf, sizeToSend);

          sentSize = sentSize + sizeToSend;
        }
        logFile.close();
        SPIFFS.end();
        return;
      }
    }
    SPIFFS.end();
  }
  // In case of error log file
  wifi::getWifiManager().server.get()->send(200, "text/plane", "No file");
}

void eraseLogFile()
{
  if (SPIFFS.begin())
  {
    if (SPIFFS.exists("/log.txt"))
    {
      // Erase the content of the file
      File logFile = SPIFFS.open("/log.txt", "w");
      if (logFile)
        logFile.close();
    }
    SPIFFS.end();
  }
  // Send a redirection to the param page
  /*
    wifi::getWifiManager().server.get()->sendHeader("Location", "/param",true);
    wifi::getWifiManager().server.get()->send(303);
  */
  wifi::getWifiManager().server.get()->send(200, "application/x-binary", "");
}

void updateParams()
{
  logStream.setLogOutput(wifi::getParamValueFromID("logOutput"));
  if (logStream.logOutput == LogStream::LogToTelnet)
    enableTelnet(); // Enable telnet if logging to Telnet
  else
    disableTelnet();  // Telnet disabled by default
}

void waitForTelnetClient()
{
  // Wait the tenet client to connect (maximum 40 seconds)
  int i = 0;
  while (i < 40)
  {
    // If already client connected
    if (Telnet && Telnet.connected())
      break;
    else if (TelnetServer->hasClient())
    {
      // If a client is waiting, we accept him
      Telnet = TelnetServer->available(); // ready for new client
      break;
    }
    delay(1000);
    Serial.println("Waiting for the Telnet client");
    i++;
  }
}
}
