#pragma once


class MqttPublish {
public:
	MqttPublish() {
	}
  void 	start(String hostname);

	void poll();

	void publish(const char *name, const char *value);
  void publish(const String& name, const String& value);

};

