#include "wifi.h"
#include "logging.h"
#include "light.h"
#include "config.h"
#include "mqtt.h"
#include "switches.h"
#include "ESP8266TimerInterrupt.h"


namespace switches {

  #define TEMPERATURE_SENSOR A0   // Correct value

  #define TOGGLE_BUTTON 2
  #define PUSH_BUTTON   1
  
  // The possible states for the switches
  #define BUTTON_OFF                  0
  #define BUTTON_ON                   1
  #define BUTTON_OFF_ON_OFF           2
  #define BUTTON_ON_OFF_ON            3
  #define BUTTON_SHORT_CLICK          4
  #define BUTTON_LONG_CLICK           5
  #define BUTTON_DOUBLE_CLICK         6
  char *BUTTON_STATE_STR[] = { "BUTTON_OFF", "BUTTON_ON",  "BUTTON_OFF_ON_OFF", "BUTTON_ON_OFF_ON", "BUTTON_SHORT_CLICK", "BUTTON_LONG_CLICK", "BUTTON_DOUBLE_CLICK"};

  
  #define ALREADY_PUBLISHED           255
  #define NO_CHANGE                   255

/*
  #define INTERRUP_TIME       10      // Every 10 ms
  #define SLOW_LED_BLINKING   25      // 250 ms
  #define FAST_LED_BLINKNG    10      // 100 ms
  #define LONG_CLICK_DURATION 50      // 500 ms to detect long click
  #define DEBOUNCE_DURATION   20      // 200 ms
  */
  #define INTERRUP_TIME       25      // Every 25 ms
  #define SLOW_LED_BLINKING   10      // 250 ms
  #define FAST_LED_BLINKNG    4       // 100 ms
  #define LONG_CLICK_DURATION 20      // 500 ms to detect long click
  #define DEBOUNCE_DURATION   4       // 100 ms. If this value is too large, it does not detect double click
  
  ESP8266Timer ITimer;      // For the builtin Leb blinking

  float temperature;        // Internal temperature
  bool overheatingAlarm = false;
  bool mqttOverheatingAlarm = false;

  // The switch parameters
  volatile uint8_t switchType=TOGGLE_BUTTON;
  volatile uint8_t defaultSwitchReleaseState=LOW;
  bool temperatureLogging=true;

  // Getter
  float &getTemperature(){return temperature;}
  bool &getTemperatureLogging(){return temperatureLogging;}
  
  // For the temperature
  unsigned long prevTime = millis();

  // For the LED switching
  volatile uint8_t ledBlinkDuration=0;
  volatile uint8_t ledBlinkTickCounter=0;


  // For computing the current state for the switch 
  #ifdef SHELLY_SW0
  volatile uint8_t sw0StateFrame[5];
  volatile uint8_t sw0StateFrameDuration[5];
  #endif

  #ifdef SHELLY_SW1
  volatile uint8_t sw1StateFrame[5];
  volatile uint8_t sw1StateFrameDuration[5];
  #endif

  #ifdef SHELLY_SW2
  volatile uint8_t sw2StateFrame[5];
  volatile uint8_t sw2StateFrameDuration[5];
  #endif

  // The current state of the switch
  volatile uint8_t sw0State=ALREADY_PUBLISHED;
  volatile uint8_t sw1State=ALREADY_PUBLISHED;
  volatile uint8_t sw2State=ALREADY_PUBLISHED;

  void enableBuiltinLedBlinking(uint8_t ledMode)
  {
    ledBlinkTickCounter=0;
    pinMode(SHELLY_BUILTIN_LED, OUTPUT);
    switch(ledMode)
    {
      case LED_OFF:
      ledBlinkDuration=0;         // No blinking
      digitalWrite(SHELLY_BUILTIN_LED, HIGH);
      break;
      case LED_FAST_BLINKING:
      ledBlinkDuration=4;         // 100 ms
      break;
      case LED_SLOW_BLINKING:
      ledBlinkDuration=10;         // 250 ms
      break;
      case LED_ON:
      ledBlinkDuration=0;         // No blinking
      digitalWrite(SHELLY_BUILTIN_LED, LOW);
      break;
    }
  }
  
  volatile uint8_t &getSwState(uint8_t switchID)
  {
    uint8_t temp;
    switch(switchID)
    {
      case 0:
      return sw0State;
      case 1:
      return sw1State;
      case 2:
      return sw2State;
    }
    logging::getLogStream().printf("switches: wrong switchID for getSwState().\n");
    return sw0State;
  }

  volatile uint8_t ICACHE_RAM_ATTR processFrame(volatile uint8_t newState, volatile uint8_t *swStateFrame, volatile uint8_t *swStateFrameDuration)
  {
    // For debouncing
    if (swStateFrameDuration[4]<DEBOUNCE_DURATION)
    {
      swStateFrameDuration[4]++;
    }
    else if (newState != swStateFrame[4])
    {
      swStateFrame[0]=swStateFrame[1];
      swStateFrame[1]=swStateFrame[2];
      swStateFrame[2]=swStateFrame[3];
      swStateFrame[3]=swStateFrame[4];
      swStateFrame[4]=newState;
      
      swStateFrameDuration[0]=swStateFrameDuration[1];
      swStateFrameDuration[1]=swStateFrameDuration[2];
      swStateFrameDuration[2]=swStateFrameDuration[3];
      swStateFrameDuration[3]=swStateFrameDuration[4];
      swStateFrameDuration[4]=1;
    }
    else if (swStateFrameDuration[4]<254)
      swStateFrameDuration[4]++;

    if (switchType==TOGGLE_BUTTON)
    {
      // Toggle button
      if (swStateFrame[3]!=defaultSwitchReleaseState && swStateFrameDuration[3]<LONG_CLICK_DURATION && swStateFrame[4]==defaultSwitchReleaseState && swStateFrameDuration[4]==1)
      {
        light::lightToggle();
        if (light::lightIsOn())
          return BUTTON_ON_OFF_ON;
        else
          return BUTTON_OFF_ON_OFF;
      }
      else if (swStateFrame[3]==defaultSwitchReleaseState && swStateFrameDuration[3]<LONG_CLICK_DURATION && swStateFrame[4]!=defaultSwitchReleaseState && swStateFrameDuration[4]==1)
      {
        light::lightToggle();
        if (light::lightIsOn())
          return BUTTON_ON_OFF_ON;
        else
          return BUTTON_OFF_ON_OFF;
      }
      else if (swStateFrame[4]!=defaultSwitchReleaseState && swStateFrameDuration[4]==1)
      {
        light::lightToggle();
        if (light::lightIsOn())
          return BUTTON_ON;
        else
          return BUTTON_OFF;
      }
      else if (swStateFrame[4]==defaultSwitchReleaseState && swStateFrameDuration[4]==1)
      {
        light::lightToggle();
        if (light::lightIsOn())
          return BUTTON_ON;
        else
          return BUTTON_OFF;
      }
    }
    else
    {
      // Push button
      // Detect double click -> does not work
      if (swStateFrame[0]==defaultSwitchReleaseState &&
          swStateFrame[1]!=defaultSwitchReleaseState && swStateFrameDuration[1]<LONG_CLICK_DURATION &&
          swStateFrame[2]==defaultSwitchReleaseState && swStateFrameDuration[2]<LONG_CLICK_DURATION &&
          swStateFrame[3]!=defaultSwitchReleaseState && swStateFrameDuration[3]<LONG_CLICK_DURATION &&
          swStateFrame[4]==defaultSwitchReleaseState && swStateFrameDuration[4]==1)
      {
        light::lightToggle();
        return BUTTON_DOUBLE_CLICK;
      }
      else if (swStateFrame[3]!=defaultSwitchReleaseState && swStateFrameDuration[3]<LONG_CLICK_DURATION &&
               swStateFrame[4]==defaultSwitchReleaseState && swStateFrameDuration[4]==1)
      {
        // short click
        light::lightToggle();
        return BUTTON_SHORT_CLICK;
      }
      else if (swStateFrame[3]==defaultSwitchReleaseState && swStateFrame[4]!=defaultSwitchReleaseState && swStateFrameDuration[4]==LONG_CLICK_DURATION)
      {
        // Long click
        light::lightToggle();
        return BUTTON_LONG_CLICK;
      }
    }
    return NO_CHANGE;
  }
  
  void ICACHE_RAM_ATTR checkSwitch(void)
  {
    volatile uint8_t newState,tmp;

    #ifdef SHELLY_SW0
    newState=digitalRead(SHELLY_SW0);
    tmp=processFrame(newState, sw0StateFrame, sw0StateFrameDuration);
    if (tmp!=NO_CHANGE)
      sw0State=tmp;
    #endif
    
    #ifdef SHELLY_SW1
    newState=digitalRead(SHELLY_SW1);
    tmp=processFrame(newState, sw1StateFrame, sw1StateFrameDuration);
    if (tmp!=NO_CHANGE)
      sw1State=tmp;
    #endif

    #ifdef SHELLY_SW2
    newState=digitalRead(SHELLY_SW2);
    tmp=processFrame(newState, sw2StateFrame, sw2StateFrameDuration); 
    if (tmp!=NO_CHANGE)
      sw2State=tmp;   
    #endif

    // For the built-in led blinking
    if (ledBlinkDuration>0)
    {
      ledBlinkTickCounter++;
      if (ledBlinkTickCounter>ledBlinkDuration)
      {
        digitalWrite(SHELLY_BUILTIN_LED, !(digitalRead(SHELLY_BUILTIN_LED)));  //Invert Current State of LED
        ledBlinkTickCounter=0;
      }
    }
  }
  
  
  void setup()
  {
    uint8_t state;
    
    #ifdef SHELLY_SW0
    pinMode(SHELLY_SW0, INPUT_PULLUP);  // only works with INPUT_PULLUP
    state=digitalRead(SHELLY_SW0);
    for (int i=0;i<sizeof(sw0StateFrame);i++)
    {
      sw0StateFrameDuration[i]=255;
      sw0StateFrame[i]=state;
    }
    #endif

    #ifdef SHELLY_SW1
    pinMode(SHELLY_SW1, INPUT);
    state=digitalRead(SHELLY_SW1);
    // Initialise the frame with the current state of the switch
    // By default, the light is off when the switch is powering on
    for (int i=0;i<sizeof(sw1StateFrame);i++)
    {
      sw1StateFrameDuration[i]=255;
      sw1StateFrame[i]=state;
    }
    #endif
    
    #ifdef SHELLY_SW2
    pinMode(SHELLY_SW2, INPUT);
    state=digitalRead(SHELLY_SW2);
    for (int i=0;i<sizeof(sw2StateFrame);i++)
    {
      sw2StateFrameDuration[i]=255;
      sw2StateFrame[i]=state;
    }
    #endif
    
    // Interrup every 25 ms, misses click with 50 ms
    ITimer.attachInterruptInterval(1000 * INTERRUP_TIME, checkSwitch);
  }

  void overheating(int temperature)
  {
    // If above 95, the light is switched off
    if (temperature>95.0)
    {
      logging::getLogStream().printf("temperature: overheating; the light is switched off.\n");
      light::setBrightness(0);            // Brightness to 0
      light::setBlinkingDuration(0);      // Stop blinking
    }
    overheatingAlarm = true;
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

  void updateParams()
  {
    logging::getLogStream().println("switches: updateParams");
    setSwitchType(wifi::getParamValueFromID("switchType"));
    setDefaultSwitchReleaseState(wifi::getParamValueFromID("defaultReleaseState"));
  }

  void publishMQTTChangeSwitch(uint8_t switchID)
  {
    if (getSwState(switchID)!=ALREADY_PUBLISHED)
    {
      const char* topic=wifi::getParamValueFromID("pubMqttSwitchEvents");
      // If no topic, we do not publish
      if (topic!=NULL)
      {
        char payload[50];
        if (light::lightIsOn())
          sprintf(payload,"%s LIGHT_ON",BUTTON_STATE_STR[getSwState(switchID)]);
        else
          sprintf(payload,"%s LIGHT_OFF",BUTTON_STATE_STR[getSwState(switchID)]);
        if (mqtt::publishMQTT(topic,payload))
          getSwState(switchID)=ALREADY_PUBLISHED;
      }
    }
  }
  
  void handle()
  { 
    // Publish new values to MQTT if needed
    #ifdef SHELLY_SW0
    publishMQTTChangeSwitch(0);
    #endif
    #ifdef SHELLY_SW1
    publishMQTTChangeSwitch(1);
    #endif
    #ifdef SHELLY_SW2
    publishMQTTChangeSwitch(2);
    #endif
    
    // Check the internal temperature every 1 second
    unsigned long now=millis();
    if(now - prevTime > 1000)
    {
        prevTime = now;
        temperature = readTemperature();
        if (temperatureLogging)
          logging::getLogStream().printf("temperature: %f\n", temperature);
        // If temperature is above 95°C, the light is switched off
        if (temperature>80.0)
          overheating(temperature);
        else
          overheatingAlarm=false;
    }

    // Publish MQTT overheating alarm
    if (overheatingAlarm==true && mqttOverheatingAlarm==false)
    {
      // Publish the MQTT alarm
      const char* topic = wifi::getParamValueFromID("pubMqttAlarmOverheat");
      // If no topic, we do not publish
      if (topic!=NULL)
      {
        const char* hn = wifi::getParamValueFromID("hostname");
        char payload[8];
        if (hn!=NULL)
          sprintf(payload, "\"%s\" %f", hn, temperature);
        else
          sprintf(payload, "%f", temperature);        
        if (mqtt::publishMQTT(topic, payload, 1))
          mqttOverheatingAlarm=true;
      }
    }
    if (overheatingAlarm==false && mqttOverheatingAlarm==true)
    {
      mqttOverheatingAlarm=false;
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
