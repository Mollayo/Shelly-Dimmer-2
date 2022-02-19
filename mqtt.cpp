#include <Arduino.h>

#include "config.h"
#include "wifi.h"
#include "logging.h"
#include "switches.h"
#include "light.h"
#include "mqtt.h"

/*
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
*/
#include <PubSubClient.h>


namespace mqtt
{
WiFiClient wifiClient;
//Adafruit_MQTT *mqttClient = NULL;
PubSubClient *mqttClient = NULL;

char mqttClientId[18] = {0x00};  //98_F4_AB_B9_8A_73
#define NB_MAX_SUBSCRIBE 7
//Adafruit_MQTT_Subscribe *mqttSubscribe[NB_MAX_SUBSCRIBE] = {NULL};

// For the MQTT broker
unsigned long lastReconnectAttemptTime = 0;
unsigned long lastTempPublishTime = 0;      // For pubishing the temperature at regular time
const char* mqttServerIP;
uint16 mqttPort = 0;
char receivedMqttMsg[100];


void callback(char* topic, byte* msg, unsigned int length)
{
  if (length>sizeof(receivedMqttMsg)+1)
  {
    memcpy(receivedMqttMsg,msg,sizeof(receivedMqttMsg)-1);
    receivedMqttMsg[sizeof(receivedMqttMsg)-1]=0x00;
    logging::getLogStream().printf("mqtt: error msg too long \"%s\" and payload \"%s\"\n", topic, receivedMqttMsg);
    return;
  }
  memcpy(receivedMqttMsg,msg,length);
  receivedMqttMsg[length]=0x00;
  // handle message arrived
  logging::getLogStream().printf("mqtt: receiving a message with topic \"%s\" and payload \"%s\"\n", topic, receivedMqttMsg);

  // find to which functionnality this topic is associated with
  const char* paramID = wifi::getIDFromParamValue(topic);
  if (paramID != NULL)
    light::mqttCallback(paramID, (char*)receivedMqttMsg);
}

void updateParams()
{
  logging::getLogStream().printf("mqtt: updateParams\n");

  // Disconnect the mqttClient if it is connected
  if (mqttClient != NULL)
  {
    logging::getLogStream().printf("mqtt: disconnect from %s:%d\n", mqttServerIP, mqttPort);
    // delete the mqttClient
    mqttClient->disconnect();
    delete mqttClient;
    mqttClient = NULL;
  }
  // Get the broker and port from wifiManager
  const char* buff = wifi::getParamValueFromID("mqttPort");
  if (buff != NULL)
    mqttPort = atoi(buff);
  // Set the new MQTT sever configuration
  mqttServerIP = wifi::getParamValueFromID("mqttServer");
  if (mqttServerIP != NULL && strlen(mqttServerIP) > 0)
  {
    logging::getLogStream().printf("mqtt: set the new MQTT broker to %s:%d\n", mqttServerIP, mqttPort);
    uint8_t mac[6];   // 98_F4_AB_B9_8A_73
    WiFi.macAddress(mac);
    const char* tmp=helpers::hexToStr(mac, 6);
    memcpy(mqttClientId,tmp,sizeof(mqttClientId));
    logging::getLogStream().printf("mqtt: MQTT cliend Id %s\n", mqttClientId);
    mqttClient = new PubSubClient(wifiClient);
    mqttClient->setServer(mqttServerIP, mqttPort);
    mqttClient->setCallback(callback);
    //mqttClient = new Adafruit_MQTT_Client(&wifiClient, mqttServerIP, mqttPort, mqttClientId, "", "");
  }
  else
    logging::getLogStream().printf("mqtt: MQTT broker not defined\n");
}

void setup()
{
}

bool publishMQTT(const char *topic, const char *payload)
{
  if (mqttClient == NULL)
    return false;
  if (mqttClient->publish(topic, payload))
  {
    logging::getLogStream().printf("mqtt: publishing with topic \"%s\" and payload \"%s\"\n", topic, payload);
    return true;
  }
  else
  {
    // Ideally, the msg that failed to be published should be put into a buffer so that they can be published later
    logging::getLogStream().printf("mqtt: failed to publish with topic \"%s\" and payload \"%s\"\n", topic, payload);
    return false;
  }
}


void publishMQTTTempAtRegularInterval()
{
  if (mqttClient == NULL)
    return;
  unsigned long now = millis();
  if (now - lastTempPublishTime > 5000)
  {
    lastTempPublishTime = now;
    const char* topic = wifi::getParamValueFromID("pubMqttTemperature");
    // If no topic, we do not publish
    if (topic != NULL)
    {
      char payload[8];
      int temperature = switches::getTemperature();
      sprintf(payload, "%d", temperature);
      publishMQTT(topic, payload);
    }
  }
}

void connectToMQTTServer()
{
  // Attempt to reconnect
  bool ret = mqttClient->connect(mqttClientId);
  if (ret == true)
  {
    // connect will return 0 for connected
    lastReconnectAttemptTime = 0;
    logging::getLogStream().printf("mqtt: connected to %s:%d\n", mqttServerIP, mqttPort);

    // Subscribe to all the topics
    int topicIdx = 0;
    WiFiManagerParameter** customParams = wifi::getWifiManager().getParameters();
    for (int i = 0; i < wifi::getWifiManager().getParametersCount(); i++)
    {
      if (customParams[i]->getID() == NULL)
        continue;
      if (strncmp(customParams[i]->getID(), "subMqtt", 7) != 0)
        continue;
      if (customParams[i]->getValue() == NULL)
        continue;
      if (strlen(customParams[i]->getValue()) == 0)
        continue;

      //mqttSubscribe[topicIdx] = new Adafruit_MQTT_Subscribe(mqttClient, customParams[i]->getValue(), 2);        // QoS=2
      mqttClient->subscribe(customParams[i]->getValue());
      topicIdx++;
      logging::getLogStream().printf("mqtt: subscribing to %s\n", customParams[i]->getValue());
    }
  }
  else
    logging::getLogStream().printf("mqtt: failed to connect to %s:%d\n", mqttServerIP, mqttPort);
}

void handle()
{      
  // If the MQTT server has been defined
  if (mqttClient != NULL)
  {
    // If not connected
    if (!mqttClient->connected())
    {
      unsigned long now = millis();
      if (now - lastReconnectAttemptTime > 5000)
      {
        lastReconnectAttemptTime = now;
        connectToMQTTServer();
      }
    }
    else
    {
      // mqttClient connected, check for the topics that have been subscribed
      mqttClient->loop();

      // Publish the temperature at regular interval
      publishMQTTTempAtRegularInterval();
    }
  }

}
}
