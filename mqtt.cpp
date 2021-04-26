#include <Arduino.h>
#ifdef __ESP8266__
#include <ESP8266WiFi.h>
#else
#include "WiFi.h"
#endif
#include "mqtt.h"

#include <PubSubClient.h>

WiFiClient wfc;

PubSubClient client(wfc);

void MqttPublish::start(IPAddress ip, uint16_t port,  String server_name)
{
  client.setServer(ip, port);
  clientId = server_name;
  client.connect(server_name.c_str());
}

void MqttPublish::reconnect() {
  // Loop until we're reconnected
  if (!client.connected()) {
    log.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      log.println("connected");
      for (int i=0;i<sub.size();++i)
    	  client.subscribe(sub[i].c_str());
    } else {
      log.print("failed, rc=");
      log.print(client.state());
      log.println(" try again in 5 seconds");
    }
  }
}
long lastMsg;
void MqttPublish::poll()
{
  client.loop();
  long now = millis();
  if (now - lastMsg > 60000) {
    lastMsg = now;
    reconnect();    
  }
}

void MqttPublish::publish(const String &name, const String &val)
{
  if (client.connected()) {
    client.publish(name.c_str(), val.c_str());
  }
}
void MqttPublish::setcallback(MQTT_CALLBACK_SIGNATURE)
{
	client.setCallback(callback);
}

void MqttPublish::subscribe(const String &name)
{
	sub.push_back(name);
  client.subscribe(name.c_str());
}
