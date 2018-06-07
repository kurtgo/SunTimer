#include <Arduino.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_MCP9808.h>
#include <SparkFun_Si7021_Breakout_Library.h>
#include "temp.h"

Weather *temp;
DHT *have_dht;
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();

void AbstractTemp::begin(Print &Serial)
{

	  switch(m_type) {
	    case type_notemp: return;
	    case type_DHT11: {
			have_dht = new DHT(2,DHT11);
			have_dht->begin();
      have_temp=1;
      have_hum=1;
	    }
	    break;
	    case type_MCP9808: {
    	if (!tempsensor.begin()) {
    		Serial.println("Couldn't find MCP9808!");
    	} else {
    		Serial.println("Found MCP9808");
    		have_temp = 1;
    	}
	    }
	    break;
	    case type_si7021: {
	    	temp = new Weather();
	    	if (!temp->begin()) {
	    		Serial.println("Couldn't find si7021!");
	    	} else {
	    		Serial.println("Found si7021");
          have_hum=1;
	    		have_temp = 1;
	    	}
	    }
	    break;
	  }
}
float AbstractTemp::GetTemp()
{
  switch(m_type) {
    case type_notemp: return 0;
    case type_DHT11: return have_dht->readTemperature(true);
    case type_MCP9808: {
		float c = tempsensor.readTempC();
		float f = c * 9.0 / 5.0 + 32;
		return f;
    }
    case type_si7021: return temp->getTempF();
  }
  return 0.0;
}
float AbstractTemp::GetHumidity()
{
  switch(m_type) {
    case type_notemp: break;
    case type_DHT11: return have_dht->readHumidity(true);
    case type_MCP9808: break;
    case type_si7021: return temp->getRH();
  }
  return 0.0;
}
