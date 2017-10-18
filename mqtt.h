#pragma once


class MqttPublish {
protected:
  Print& log;
  void reconnect();
  String clientId;
public:
	MqttPublish(Print &debug) : log(debug) {
    
	}
  void 	start(IPAddress server, uint16_t port, String hostname);

	void poll();

	void publish(const char *name, const char *value);
  void publish(const String& name, const String& value);

};

