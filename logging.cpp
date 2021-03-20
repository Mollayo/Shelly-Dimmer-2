#include "logging.h"

#include <ESP8266WiFi.h>
#include "config.h"
#include "wifi.h"
#include "light.h"
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
}

size_t LogStream::write(uint8_t data)
{
  size_t tmp = 0;
  File logFile;
  switch (logOutput)
  {
    case LogToSerial:
      return Serial.write(data);
    case LogToTelnet:
      return Telnet.write(data);
    case LogToFile:
      logFile = SPIFFS.open("/log.txt", "a");
      if (logFile)
      {
        tmp = logFile.write(data);
        logFile.close();
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

  const uint8_t MAXBUFFERSIZE = 40;
  if (Telnet && Telnet.connected() && Telnet.available())
  {
    char c;
    uint8_t charsReceived = 0;
    if (telnetCmd == NULL)
      telnetCmd = (char*)calloc(sizeof(char), MAXBUFFERSIZE);
    memset(telnetCmd,0x00,sizeof(char)*MAXBUFFERSIZE);

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
    Telnet.println(" res : reset the STM32 MCU");
    Telnet.println(" s : get the state of the STM32 MCU");
    Telnet.println(" v : get the version of the STM32 MCU");    
    Telnet.println(" br000 to br100 : set the brightness between 0% and 100%");
    Telnet.println(" on or off : switch on/off the light");
    Telnet.println(" temp : enable/disable temperature logging and overheating alarm");
    Telnet.println(" blpt xxx xxx xxx : set blinking pattern");
    Telnet.println(" sab : start blinking");
    Telnet.println(" sob : stop blinking");
    Telnet.println(" bldu : set the blinking duration");
  }
}

void handle()
{
  char* telnetCmd = readTelnetCmd();
  if (telnetCmd != NULL)
  {
    // 's' to send the "get state" command
    if (telnetCmd[0] == 's' && telnetCmd[1] == 0x0D)
      light::sendCmdGetState();
    else if (telnetCmd[0] == 'b' && telnetCmd[1] == 'r' && telnetCmd[5] == 0x0D)
    {
      // '0' to '9' to set the brightness from 0% to 90%
      uint16_t v = (telnetCmd[2] - '0') * 100 + (telnetCmd[3] - '0') * 10 + (telnetCmd[4] - '0');
      if (v >= 0 && v <= 100)
        light::setBrightness(v);
      else
        logging::getLogStream().printf("wrong value for the brightness: %d\n", v);
    }
    else if (telnetCmd[0] == 'v' && telnetCmd[1] == 0x0D)
      light::sendCmdGetVersion();
    else if (telnetCmd[0] == 'o' && telnetCmd[1] == 'n' && telnetCmd[2] == 0x0D)
      light::lightOn();
    else if (telnetCmd[0] == 'o' && telnetCmd[1] == 'f' && telnetCmd[2] == 'f' && telnetCmd[3] == 0x0D)
      light::lightOff();
    else if (telnetCmd[0] == 't' && telnetCmd[1] == 'e'&& telnetCmd[2] == 'm' && telnetCmd[3] == 'p' && telnetCmd[4] == 0x0D)
      switches::getTemperatureLogging()=!switches::getTemperatureLogging();
    else if (telnetCmd[0] == 'r' && telnetCmd[1] == 'e' && telnetCmd[2] == 's' && telnetCmd[3] == 0x0D)
      light::STM32reset();
    else if (telnetCmd[0] == 's' && telnetCmd[1] == 'a' && telnetCmd[2] == 'b' && telnetCmd[3] == 0x0D)
      light::startBlinking();
    else if (telnetCmd[0] == 's' && telnetCmd[1] == 'o' && telnetCmd[2] == 'b' && telnetCmd[3] == 0x0D)
      light::stopBlinking();
    else if (telnetCmd[0] == 'b' && telnetCmd[1] == 'l' && telnetCmd[2] == 'p' && telnetCmd[3] == 't' && telnetCmd[4] == ' ')
      light::setBlinkingPattern(telnetCmd+5);
    else if (telnetCmd[0] == 'b' && telnetCmd[1] == 'l' && telnetCmd[2] == 'd' && telnetCmd[3] == 'u' && telnetCmd[4] == ' ')
      light::setBlinkingDuration(telnetCmd+5);
    else
      // Command not recognized command, we print the menu options
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


void eraseLogFile()
{
  if (SPIFFS.exists("/log.txt"))
  {
    // Erase the content of the file
    File logFile = SPIFFS.open("/log.txt", "w");
    if (logFile)
      logFile.close();
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
