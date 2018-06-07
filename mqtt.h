#pragma once

#include <PubSubClient.h>
#include "vector.h"

class MqttPublish {
protected:
  Print& log;
  void reconnect();
  Vector<String> sub;
  String clientId;
public:
	MqttPublish(Print &debug) : log(debug) {
    
	}
  void 	start(IPAddress server, uint16_t port, String hostname);

	void poll();

	void publish(const char *name, const char *value);
  void publish(const String& name, const String& value);
  void subscribe(const String& name);
  void setcallback(MQTT_CALLBACK_SIGNATURE);
  

};
