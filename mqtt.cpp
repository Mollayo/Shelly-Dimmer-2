#include "config.h"
#include "wifi.h"
#include "logging.h"
#include "switches.h"
#include "dimmer.h"
#include <PubSubClient.h>
#include "mqtt.h"


namespace mqtt
{
  WiFiClient wifiClient;
  PubSubClient client(wifiClient);

  // For the MQTT broker
  long lastReconnectAttempt = 0;
  const char* mqttServer;
  uint16 mqttPort=0;
  
  
  void callback(char* topic, byte* payload, unsigned int length) 
  {
    // handle message arrived
    char msg[length+1];
    memcpy(msg,payload,length);
    msg[length]=0x00;
    logging::getLogStream().printf("mqtt: receiving a message with topic \"%s\" and payload \"%s\"\n", topic, msg);

    // find to which functionnality this topic is associated with
    const char* paramID=wifi::getIDFromParamValue(topic);
    if (paramID!=NULL)
    {
      if (strcmp(paramID,"subMqttLightOn") == 0)
        dimmer::switchOn();
      else if (strcmp(paramID,"subMqttLightAllOn") == 0)
        dimmer::switchOn();
      else if (strcmp(paramID,"subMqttLightOff") == 0)
        dimmer::switchOff();
      else if (strcmp(paramID,"subMqttLightAllOff") == 0)
        dimmer::switchOff();
      else if (strcmp(paramID,"subMqttStartBlink") == 0)
        dimmer::setBlinkingDuration(1000);
      else if (strcmp(paramID,"subMqttStartFastBlink") == 0)
        dimmer::setBlinkingDuration(500);
      else if (strcmp(paramID,"subMqttStopBlink") == 0)
        dimmer::setBlinkingDuration(0);
    }
  }
  
  void configure()
  {
     logging::getLogStream().printf("mqtt: configure\n");
    // Disconnect the client if it is connected
    if (client.connected())
    {
      logging::getLogStream().printf("mqtt: disconnect from %s:%d\n",mqttServer,mqttPort);
      client.disconnect();
    }
    // Get the broker and port from wifiManager
    const char* buff=wifi::getParamValueFromID("mqttPort");
    if (buff!=NULL)
      mqttPort=atoi(buff);
    // Set the new MQTT sever configuration
    mqttServer=wifi::getParamValueFromID("mqttServer");
    if (mqttServer!=NULL && strlen(mqttServer)>0)
    {
      logging::getLogStream().printf("mqtt: set the new MQTT broker to %s:%d\n",mqttServer,mqttPort);
      client.setServer(mqttServer, mqttPort);
    }
    else
      logging::getLogStream().printf("mqtt: MQTT broker not defined\n");
  }
  
  void setup()
  {
    client.setCallback(callback);
  }

  boolean reconnect()
  {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    if (client.connect(helpers::hexToStr(mac, 6)))        // the client name is the MAC address
    {
      logging::getLogStream().printf("mqtt: connected to %s:%d with client name %s\n", mqttServer, mqttPort, helpers::hexToStr(mac, 6));
      
      // Subscribe to all the topics
      WiFiManagerParameter** customParams = wifi::getWifiManager().getParameters();
      for (int i = 0; i < wifi::getWifiManager().getParametersCount(); i++)
      {
        if (customParams[i]->getID()==NULL)
          continue;
        if (strncmp(customParams[i]->getID(), "subMqtt", 7) != 0)
          continue;
        if (customParams[i]->getValue() == NULL)
          continue;
        if (strlen(customParams[i]->getValue()) == 0)
          continue;
        
        logging::getLogStream().printf("mqtt: subscribing to %s\n",customParams[i]->getValue());
        client.subscribe(customParams[i]->getValue());
      }
    }
    else
      logging::getLogStream().printf("mqtt: failed to connect to %s:%d\n",mqttServer,mqttPort);
    return client.connected();
  }

  void publishMQTTChangeBrightness(uint16_t brightnessLevel)
  {
    const char* topic=wifi::getParamValueFromID("pubMqttBrighnessLevel");
    // If no topic, we do not publish
    if (topic==NULL)
      return;
    if (brightnessLevel>=0 && brightnessLevel<=1000)
    {
      char payload[5];
      sprintf(payload,"%d",brightnessLevel);
      client.publish(topic, payload);
      logging::getLogStream().printf("mqtt: publishing with topic \"%s\" and payload \"%s\"\n",topic, payload);
    }
    else
      // Brightness value out of range
      logging::getLogStream().printf("mqtt: error when publishing brightness. Brightness %d out of range\n",brightnessLevel);
  }

  void publishMQTTChangeSwitch(uint8 swID, uint swState)
  {
    const char* topic=wifi::getParamValueFromID("pubMqttSwitchEvents");
    // If no topic, we do not publish
    if (topic==NULL)
      return;
    if (swID>=0 && swID<10 && swState>=0 && swState<10)
    {
      char payload[4];
      sprintf(payload,"%d %d",swID,swState);
      client.publish(topic, payload);
      logging::getLogStream().printf("mqtt: publishing with topic \"%s\" and payload \"%s\"\n",topic, payload);
    }
    else
      logging::getLogStream().printf("mqtt: error when publishing brightness. Switch ID %d and/or state %d out of range\n", swID, swState);
  }

  void publishMQTTOverheating(int temperature)
  {
    const char* topic=wifi::getParamValueFromID("pubMqttOverheat");
    // If no topic, we do not publish
    if (topic==NULL)
      return;
    char payload[8];
    sprintf(payload,"%d",temperature);
    client.publish(topic, payload);
    logging::getLogStream().printf("mqtt: publishing with topic \"%s\" and payload \"%s\"\n",topic, payload);
  }
  
  void handle()
  {
    // If the MQTT server has been defined
    if (mqttServer  && strlen(mqttServer)>0)
    {
      // If not connected
      if (!client.connected())
      {
        long now = millis();
        if (now - lastReconnectAttempt > 5000) 
        {
          lastReconnectAttempt = now;
          // Attempt to reconnect
          if (reconnect()) 
            lastReconnectAttempt = 0;
          else
            logging::getLogStream().printf("mqtt: failed to connect to %s:%d\n",mqttServer,mqttPort);
        }
      }
      else 
      {
        // Client connected
        client.loop();
      }
    }

  }
}
