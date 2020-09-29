#ifndef LOGGING
#define LOGGING

#include <Arduino.h>

namespace logging
{

  class LogStream : public Stream
  {
    public:
  
      enum LogOutput { LogDisabled = 0, LogToSerial = 1, LogToTelnet = 2, LogToFile = 3 };
  
      LogStream();
      void setLogOutput(const char *c);
      virtual size_t write(uint8_t data);
      virtual int availableForWrite();
      virtual int available();
      virtual int read();
      virtual int peek();
      virtual void flush();
    public:
      LogOutput logOutput;
  };
  
  LogStream &getLogStream();

  char* handleTelnet();
  void enableTelnet();
  void disableTelnet();
  void printTelnetMenu();

  void displayFile();
  void eraseLogFile();

  void updateParams();
  void setup();
  void handle();
}

#endif
