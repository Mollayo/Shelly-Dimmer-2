#include <AceButton.h>
#include <Ticker.h>
#include "wifi.h"
#include "logging.h"
#include "dimmer.h"
#include "config.h"
#include "mqtt.h"
#include "switches.h"
#include "ESP8266TimerInterrupt.h"

using namespace ace_button;

namespace switches {
  
  
  ESP8266Timer ITimer;      // For the builtin Leb blinking

  float temperature;        // Internal temperature

  // The switch parameters
  uint8 switchType=TOGGLE_BUTTON;
  uint8 defaultSwitchReleaseState=LOW;
  bool temperatureLogging=true;

  // Getter
  float &getTemperature(){return temperature;}
  uint8 &getSwitchType(){return switchType;}
  uint8 &getDefaultSwitchReleaseState(){return defaultSwitchReleaseState;}
  bool &getTemperatureLogging(){return temperatureLogging;}
  
  // For the temperature
  unsigned long prevTime = millis();

  // For the switches
  // Parameters: pin, initial state for release state, id
  AceButton swInt(SHELLY_BUILTIN_SWITCH,HIGH,0);
  AceButton sw1(SHELLY_SW1,LOW,1);
  AceButton sw2(SHELLY_SW2,LOW,2);

  void handleSWEvent(AceButton*, uint8_t, uint8_t);


  void ICACHE_RAM_ATTR builtinLedBlinking(void)
  {
    digitalWrite(SHELLY_BUILTIN_LED, !(digitalRead(SHELLY_BUILTIN_LED)));  //Invert Current State of LED  
  }
  
  void enableBuiltinLedBlinking(bool enable)
  {
    pinMode(SHELLY_BUILTIN_LED, OUTPUT);
    if (enable)
    {
      ITimer.attachInterruptInterval(1000 * 250, builtinLedBlinking);
    }
    else
    {
      ITimer.detachInterrupt();
      digitalWrite(SHELLY_BUILTIN_LED, LOW);  // LED on
    }
  }


  void overheating()
  {
    logging::getLogStream().printf("temperature: overheating; the light is switched off.\n");
    dimmer::setBrightness(0);
    mqtt::publishMQTTOverheating(temperature);
  }

  double TaylorLog(double x)
  {
    // https://stackoverflow.com/questions/46879166/finding-the-natural-logarithm-of-a-number-using-taylor-series-in-c
  
    if (x <= 0.0) { return NAN; }
    if (x == 1.0) { return 0; }
    double z = (x + 1) / (x - 1);                              // We start from power -1, to make sure we get the right power in each iteration;
    double step = ((x - 1) * (x - 1)) / ((x + 1) * (x + 1));   // Store step to not have to calculate it each time
    double totalValue = 0;
    double powe = 1;
    for (uint32_t count = 0; count < 10; count++) {            // Experimental number of 10 iterations
      z *= step;
      double y = (1 / powe) * z;
      totalValue = totalValue + y;
      powe = powe + 2;
    }
    totalValue *= 2;
  
    return totalValue;
  }
    
  float readTemperature()
  {
    // Range: 387 (cold) to 226 (hot)
    int adc = analogRead(TEMPERATURE_SENSOR);
    
    // Shelly 2.5 NTC Thermistor
    // 3V3 --- ANALOG_NTC_BRIDGE_RESISTANCE ---v--- NTC --- Gnd
    //                                         |
    //                                        ADC0
    #define ANALOG_NTC_BRIDGE_RESISTANCE  32000            // NTC Voltage bridge resistor
    #define ANALOG_NTC_RESISTANCE         10000            // NTC Resistance
    #define ANALOG_NTC_B_COEFFICIENT      3350             // NTC Beta Coefficient
    #define TO_CELSIUS(x) ((x) - 273.15)
    #define TO_KELVIN(x) ((x) + 273.15)
    // Parameters for equation
    #define TO_CELSIUS(x) ((x) - 273.15)
    #define TO_KELVIN(x) ((x) + 273.15)
    #define ANALOG_V33                    3.3              // ESP8266 Analog voltage
    #define ANALOG_T0                     TO_KELVIN(25.0)  // 25 degrees Celcius in Kelvin (= 298.15)

    // Steinhart-Hart equation for thermistor as temperature sensor
    double Rt = (adc * ANALOG_NTC_BRIDGE_RESISTANCE) / (1024.0 * ANALOG_V33 - (double)adc);
    double BC = (double)ANALOG_NTC_B_COEFFICIENT * 10000 / 10000;
    double T = BC / (BC / ANALOG_T0 + TaylorLog(Rt / (double)ANALOG_NTC_RESISTANCE));
    double temperature = TO_CELSIUS(T);

    return temperature;
  }

  void configure()
  {
    logging::getLogStream().println("switches: configure");
    setSwitchType(wifi::getParamValueFromID("switchType"));
    setDefaultSwitchReleaseState(wifi::getParamValueFromID("defaultReleaseState"));
    
    ButtonConfig* buttonConfig;

    pinMode(SHELLY_BUILTIN_SWITCH, INPUT_PULLUP);  // only works with INPUT_PULLUP
    buttonConfig = swInt.getButtonConfig();
    buttonConfig->setFeature(ButtonConfig::kFeatureClick);
    buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
    buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
    buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);

    sw1.init(SHELLY_SW1, getDefaultSwitchReleaseState(), 1);
    pinMode(SHELLY_SW1, INPUT);
    buttonConfig = sw1.getButtonConfig();
    buttonConfig->resetFeatures();
    if (getSwitchType()==PUSH_BUTTON)
    {
      buttonConfig->setFeature(ButtonConfig::kFeatureClick);
      buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
      buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
      //buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
    }

    sw2.init(SHELLY_SW2, getDefaultSwitchReleaseState(), 2);
    pinMode(SHELLY_SW2, INPUT);
    buttonConfig = sw2.getButtonConfig();
    buttonConfig->resetFeatures();
    if (getSwitchType()==PUSH_BUTTON)
    {
      buttonConfig->setFeature(ButtonConfig::kFeatureClick);
      buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
      buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
      //buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
    }
  }

  void setup()
  {
    ButtonConfig* buttonConfig;
    buttonConfig = sw1.getButtonConfig();
    buttonConfig->setClickDelay(500);             // default: 200
    buttonConfig->setLongPressDelay(500);     // default: 1000
    buttonConfig = sw2.getButtonConfig();
    buttonConfig->setClickDelay(500);             // default: 200
    buttonConfig->setLongPressDelay(500);     // default: 1000

    // Same event handler for all switches
    buttonConfig->setEventHandler(handleSWEvent);

    configure();
  }
  
  void handle()
  { 
    // Check the switches
    swInt.check();
    sw1.check();
    sw2.check();

    // Check the internal temperature every 5 seconds
    if(millis() - prevTime > 5000)
    {
        prevTime = millis();
        switches::getTemperature() = readTemperature();
        if (temperatureLogging)
          logging::getLogStream().printf("temperature: %f\n", switches::getTemperature());
        // If temperature is above 95°C, the light is switched off
        if (switches::getTemperature()>95.0)
          overheating();
    }
  }

  void handleSWEvent(AceButton* sw, uint8_t eventType, uint8_t buttonState)
  {
    logging::getLogStream().printf("switches: eventType: %d, buttonState: %d, switchType: %d\n",eventType,buttonState,getSwitchType());
    // For the built-in switch
    if (sw->getId()==0)
    {
      return;
    }
    switch (eventType) {
      case AceButton::kEventPressed:
        if (getSwitchType()==TOGGLE_BUTTON)
        {
          logging::getLogStream().printf("button: SW%d pressed\n", sw->getId());
          dimmer::switchOn();
          mqtt::publishMQTTChangeSwitch(sw->getId(),eventType);
        }
        break;
      case AceButton::kEventReleased:
        if (getSwitchType()==TOGGLE_BUTTON)
        {
          logging::getLogStream().printf("button: SW%d released\n", sw->getId());
          dimmer::switchOff();
          mqtt::publishMQTTChangeSwitch(sw->getId(),eventType);
        }
        break;
      case AceButton::kEventClicked:
        if (getSwitchType()==PUSH_BUTTON)
        {
          logging::getLogStream().printf("button: SW%d clicked\n", sw->getId());
          dimmer::switchToggle();
          mqtt::publishMQTTChangeSwitch(sw->getId(),eventType);
        }
        break;
      case AceButton::kEventLongPressed:
        if (getSwitchType()==PUSH_BUTTON)
        {
          logging::getLogStream().printf("button: SW%d long pressed\n", sw->getId());
          // Toggle the brighness
          dimmer::switchToggle();
          mqtt::publishMQTTChangeSwitch(sw->getId(),eventType);
        }
        break;
    }
  }

  void setSwitchType(const char* str)
  {
    if (!helpers::isInteger(str,1))
      return;
    if (str[0]=='1')
      switchType=PUSH_BUTTON;
    else
      switchType=TOGGLE_BUTTON;
  }

  void setDefaultSwitchReleaseState(const char* str)
  {
    if (!helpers::isInteger(str,1))
      return;
    if (str[0]=='0')
      defaultSwitchReleaseState=LOW;
    else
      defaultSwitchReleaseState=HIGH;
  }

}
