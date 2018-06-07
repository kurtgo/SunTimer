#pragma once

class AbstractTemp {
public:
	enum TempDevice {
		type_notemp,
		type_DHT11,
		type_MCP9808,
		type_si7021
	} m_type;
 int have_temp;
 int have_hum;
	AbstractTemp(TempDevice type)
	{
     m_type = type;
     have_temp = 0;
     have_hum = 0;
	}
	
	void begin(Print &output);
	float GetTemp();
	float GetHumidity();
	bool haveTemp() {return have_temp;}
	bool haveHumidity() {return have_hum;}
};
