#include <Arduino.h>

#include "config.h"
#include "wifi.h"
#include "logging.h"
#include "switches.h"
#include "light.h"
#include "mqtt.h"

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// Keep the MQTT connection the longuest possible, this should be done in the "Adafruit_MQTT.h" file
//#define MQTT_CONN_KEEPALIVE 0xFFFF

namespace mqtt
{
WiFiClient wifiClient;
Adafruit_MQTT *mqttClient = NULL;
#define NB_MAX_SUBSCRIBE 7
Adafruit_MQTT_Subscribe *mqttSubscribe[NB_MAX_SUBSCRIBE] = {NULL};

// For the MQTT broker
unsigned long lastReconnectAttemptTime = 0;
unsigned long lastTempPublishTime = 0;      // For pubishing the temperature at regular time
unsigned long lastMQTTPublishTime = 0;      // For the ping to the broker to keep alive the connection
const char* mqttServerIP;
uint16 mqttPort = 0;
bool doPublishConnectingToBroker = false;


void callback(const char* topic, char* msg)
{
  // handle message arrived
  logging::getLogStream().printf("mqtt: receiving a message with topic \"%s\" and payload \"%s\"\n", topic, msg);

  // find to which functionnality this topic is associated with
  const char* paramID = wifi::getIDFromParamValue(topic);
  if (paramID != NULL)
    light::mqttCallback(paramID, msg);
}

void updateParams()
{
  logging::getLogStream().printf("mqtt: updateParams\n");

  // Disconnect the mqttClient if it is connected
  if (mqttClient != NULL)
  {
    logging::getLogStream().printf("mqtt: disconnect from %s:%d\n", mqttServerIP, mqttPort);
    // delete all the topics
    for (int i = 0; i < NB_MAX_SUBSCRIBE; i++)
    {
      if (mqttSubscribe[i] != NULL)
      {
        mqttClient->unsubscribe(mqttSubscribe[i]);
        delete mqttSubscribe[i];
        mqttSubscribe[i] = NULL;
      }
    }

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
    mqttClient = new Adafruit_MQTT_Client(&wifiClient, mqttServerIP, mqttPort);

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

      mqttSubscribe[topicIdx] = new Adafruit_MQTT_Subscribe(mqttClient, customParams[i]->getValue(), 2);        // QoS=2
      mqttClient->subscribe(mqttSubscribe[topicIdx]);
      topicIdx++;
      logging::getLogStream().printf("mqtt: subscribing to %s\n", customParams[i]->getValue());
    }
  }
  else
    logging::getLogStream().printf("mqtt: MQTT broker not defined\n");
}

void setup()
{
}

bool publishMQTT(const char *topic, const char *payload, int QoS)
{
  if (mqttClient == NULL)
    return false;
  if (mqttClient->publish(topic, payload, QoS))
  {
    lastMQTTPublishTime=millis();
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

void publishMQTTConnectingToBroker()
{
  if (doPublishConnectingToBroker == false)
    return;
  const char* topic = wifi::getParamValueFromID("pubMqttConnecting");
  if (topic != NULL)
  {
    // Publish its IP address
    char payload[20];
    sprintf(payload, "%s", WiFi.localIP().toString().c_str());
    if (publishMQTT(topic, payload, 1))
      doPublishConnectingToBroker = false;    // To indicate that this has been published successfully
  }
}

void keepConnectionAlive()
{
  if (mqttClient == NULL)
    return;
  unsigned long now = millis();
  if ((now - lastMQTTPublishTime)/1000 > MQTT_CONN_KEEPALIVE)
  {
    if(! mqttClient->ping()) 
    {
      logging::getLogStream().printf("mqtt: ping failed. Will disconnect\n");
      mqttClient->disconnect();
    }
    else
    {
      logging::getLogStream().printf("mqtt: ping to the broker\n");
      lastMQTTPublishTime=now;
    }
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
      publishMQTT(topic, payload, 0);
    }
    // Publish the message "connecting to the borker"
    publishMQTTConnectingToBroker();
  }
}

void handle()
{
  // Keep connection alive
  keepConnectionAlive();
      
  // If the MQTT server has been defined
  if (mqttClient != NULL)
  {
    // If not connected
    if (!mqttClient->connected())
    {
      mqttClient->disconnect();
      unsigned long now = millis();
      if (now - lastReconnectAttemptTime > 5000)
      {
        lastReconnectAttemptTime = now;
        // Attempt to reconnect
        uint8_t ret = mqttClient->connect();
        if (ret == 0)
        {
          // connect will return 0 for connected
          lastReconnectAttemptTime = 0;
          logging::getLogStream().printf("mqtt: connected to %s:%d\n", mqttServerIP, mqttPort);
          const char* topic = wifi::getParamValueFromID("pubMqttConnecting");
          // Publish the message "connected to broker" if the topic is defined
          doPublishConnectingToBroker = (topic != NULL);
        }
        else
          logging::getLogStream().printf("mqtt: failed to connect to %s:%d with error %s\n", mqttServerIP, mqttPort, mqttClient->connectErrorString(ret));
      }
    }
    else
    {
      // mqttClient connected, check for the topics that have been subscribed
      Adafruit_MQTT_Subscribe *subscription = mqttClient->readSubscription();
      if (subscription != NULL)
      {
        // If MQTT message received, call the callback
        callback(subscription->topic, (char*)subscription->lastread);
      }

      // Publish the temperature at regular interval
      publishMQTTTempAtRegularInterval();
    }
  }

}
}
