#include <Arduino.h>
#include <ESP8266WiFi.h>
#define LOG MySerial.println
#include "mqtt.h"
#include <ESP8266MQTTClient.h>

MQTTClient tmqtt;

void MqttPublish::start(String server_name)
{
    //topic, data, data is continuing
  tmqtt.onData([](String topic, String data, bool cont) {
    Serial.printf("Data received, topic: %s, data: %s\r\n", topic.c_str(), data.c_str());
    tmqtt.unSubscribe("/qos0");
  });

  tmqtt.onSubscribe([](int sub_id) {
    Serial.printf("Subscribe topic id: %d ok\r\n", sub_id);
    tmqtt.publish("/qos0", "qos0", 0, 0);
  });
  tmqtt.onConnect([]() {
    Serial.printf("MQTT: Connected\r\n");
    Serial.printf("Subscribe id: %d\r\n", tmqtt.subscribe("/qos0", 0));
  });

  tmqtt.begin("mqtt://192.168.1.199:1883");
//  mqtt.begin("mqtt://test.mosquitto.org:1883", {.lwtTopic = "hello", .lwtMsg = "offline", .lwtQos = 0, .lwtRetain = 0});
//  mqtt.begin("mqtt://user:pass@mosquito.org:1883");
//  mqtt.begin("mqtt://user:pass@mosquito.org:1883#clientId");

}

void MqttPublish::poll()
{
  tmqtt.handle();
}

void MqttPublish::publish(const String &name, const String &val)
{
 
  //tmqtt.publish(name,val,0,0);
}

