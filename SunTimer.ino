

#include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#else
#include <esp_sleep.h>
#include <esp_wifi.h>
//#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#endif
#define LOG MySerial.println
#include "mqtt.h"
//#include <ArducamSSD1306.h>
#include <Adafruit_SSD1306.h>
#include "temp.h"
#include <Wire.h>
#ifdef TEMP
#include <TempControl.h>
TempControl *tempcontrol;
#endif

AbstractTemp *atemp;
AbstractTemp::TempDevice temp_dev = AbstractTemp::type_notemp;

time_t uptime = 0;

#ifdef TFT
#include <Adafruit_ILI9341.h>
#endif

#ifdef ARDUINO_ESP8266_WEMOS_D1MINI

#define OLED_MOSI   D7 //Connect to D1 on OLED
#define OLED_CLK    D5 //Connect to D0 on OLED
#define OLED_DC     D0 //Connect to DC on OLED
#define OLED_CS     D8 //Connect to CS on OLED
#define OLED_RESET  D3 //Connect to RES on OLED
#endif

Adafruit_SSD1306 *oled = NULL;
unsigned long deepSleep = 0xffffffff; // long time to deep sleep
#include <time.h>
#include "filemgr.h"
#include "sunMoon.h"
#undef LOG
#define LOG Syslog
#include "/kghome.h"

char *subscribe=NULL;


int updateing = 0;
#ifdef TFT
Adafruit_ILI9341 *display = NULL;
#endif

//#define LEDDISP
#ifdef LEDDISP
#include <TM1638.h>
TM1638 *display = NULL;
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif
#else
void *display = NULL;
#endif


#define VERSION "0.25-esp32"
const char* defname = "suntimer23-%06x";

int sleep_enable = 0;

IPAddress syslogServer(192, 168, 1, 229);
WiFiUDP udp;

#ifndef ENABLE_PRINT
// disable Serial output

#define Serial MySerial

class SystemLog : public Print {
protected:
	char buffer[1024];
	char *last;
	int left;
public:
	SystemLog() {last = buffer;left = 1024;};
	void begin(int x) {};
	size_t write(uint8_t x) {
		write(&x, 1);
	}
	size_t write(const uint8_t *buf, size_t size)
	{
		while (size--) {
			if (*buf == '\n' || left == 0) {
				udp.beginPacket(syslogServer, 514);
				udp.write((const uint8_t*)buffer, 1024-left);
				udp.endPacket();
				left = 1024;
				last = buffer;
				++buf;
				continue;
			}
			*last++ = *buf++;
			--left;
		}
	}

};
SystemLog MySerial;
#else
#define MySerial Serial
#endif

MqttPublish mqtt(MySerial);
#define LED_BLINK led_pin

#define ESP_1_ONBOARD_LED 1
#define ESP_12_ONBOARD_LED 2

int led_pin = ESP_1_ONBOARD_LED;



//todo: get lat/lon from net : http://ip-api.com/json
#define LAT 27.9158
#define LON -82.229
#define TIMEZONE -5
#define MYEPOCH 1498500000
#define HOUR 3600
#define DAY (HOUR*24)



int updating=0;
static long last_poll = 0;
static enum {STATE_WAIT_EPOCH, STATE_HAVE_TIME, STATE_STABLE} state = STATE_WAIT_EPOCH;


class TimeEvent {
private:
	time_t fire_time;
	bool fired;

public:
	TimeEvent() {
		fire_time = 0;
		fired = false;
	}
	bool isHaveEvent(time_t now)
	{
		if (now > fire_time && !fired)
			fired = true;
		return fired;
	}
	void setEvent(time_t future)
	{
		fire_time = future;
		fired=false;
	}
	time_t getEvent()
	{
		return fire_time;
	}
};
WebServer http_server(80);
String webPage = "";
String topic = "sensor/";
String top_topic;
unsigned long a=1;
TimeEvent sunRise;
TimeEvent sunSet;


#define LIGHT_PIN light_pin
#define LIGHT_PIN_R4 12

int light_pin = 5;

#define LINKNODE_R4 0x177144

//WiFiUDP ntpUDP;
//NTPClient timeClient(ntpUDP, -5);
void Syslog(String msgtosend)
{
	unsigned int msg_length = msgtosend.length();
	byte* p = (byte*)malloc(msg_length);
	memcpy(p, (char*) msgtosend.c_str(), msg_length);

	udp.beginPacket(syslogServer, 514);
	//udp.write(ArduinoOTA.getHostname().c_str());
	udp.write(p, msg_length);
	udp.endPacket();
	free(p);
}

#ifdef update
void httpUpdate()
{
	t_httpUpdate_return ret = ESPhttpUpdate.update("192.168.1.229", 80, "/SunTimer_update.php", VERSION);
	switch(ret) {
	case HTTP_UPDATE_FAILED:
		MySerial.println("[update] Update failed.");
		break;
	case HTTP_UPDATE_NO_UPDATES:
		MySerial.println("[update] Update no Update.");
		break;
	case HTTP_UPDATE_OK:
		MySerial.println("[update] Update ok."); // may not called we reboot the ESP
		break;

	}
}
#endif
enum MODE {OFF=0, ON=1, TOGGLE=2};
int light_on = OFF;

time_t poweractive=0;
time_t sleepactive=0;
#define SLEEP_TIME 10
#define WAKE_TIME 2
void lowpower()
{
  time_t now;
  now = time(nullptr);
  if (now > poweractive) {
    digitalWrite(LIGHT_PIN, OFF);
    digitalWrite(33,OFF);
    if (sleep_enable && now > sleepactive) {
      // sleep for 10 minutes, awake for 2 mintes
      sleepactive = time(nullptr) + ((SLEEP_TIME+WAKE_TIME) * 60);
      esp_sleep_enable_timer_wakeup(60*SLEEP_TIME*1000*1000);
      esp_wifi_stop();
      esp_deep_sleep_start();
      esp_wifi_start();
      MySerial.println("Wakeup from sleep");
    }      
  }
}
void lights(int mode)
{
	switch(mode) {
	case OFF:
		light_on = OFF;
		break;
	case ON:
		light_on = ON;
		break;
	case TOGGLE:
		if (light_on == ON)
			light_on = OFF;
		else
			light_on = ON;

	}
	MySerial.print("Publish ");
	MySerial.print(" ");
	MySerial.println(light_on);
	mqtt.publish(topic, String(light_on));
	digitalWrite(LIGHT_PIN, light_on);
  digitalWrite(33,light_on==ON?OFF:ON);
  poweractive = time(nullptr) + 30;
}

class Alarm {
private:
	time_t alarm = 0;
	bool done = false;

public:
	void set(time_t t) {
		alarm = t;
		done = false;
	}
	bool fired(time_t t) {
		if (!done && t > alarm) {
			done = true;
			return true;
		}
		return false;
	}
};

//Alarm sunRise;
//Alarm sunSet;
//Alarm noon;
// Handle low power shutdown
void Shutdown()
{
	// Turn off LED in sleep mode
	digitalWrite(LED_BLINK, HIGH);
	pinMode(LED_BLINK, OUTPUT);

	// wait 10 minutes before wakeup
	ESP.deepSleep(10*60*1000000);
}

int getHour()
{
	time_t t = time(nullptr);
	return localtime(&t)->tm_hour;

}
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
	char txt[60];
	char *cur = txt;
	float temp;

	sprintf(txt, "Message arrived [%s] ",topic);
	Syslog(txt);

	sprintf(txt, "payload: %.*s", length, payload);
	Syslog(txt);
	memcpy(txt,payload,length);
	txt[length] = 0;
	temp = atof(txt);
	cur = subscribe;
	int i=0;
	while (*cur) {
		if (strcmp(cur, topic) == 0) break;
		++i;
		cur += strlen(cur)+1;
	}
#ifdef TEMP
if (tempcontrol) {
		switch(i) {
		case 0:
			Syslog("updateTemp");
			tempcontrol->updateTemp(temp, 0);
			break;
		case 1:
			Syslog("setTemp");
			tempcontrol->setTemp(temp);
			break;
		}
	    double state = tempcontrol->getstate();
		mqtt.publish(top_topic+"/hvac", String(state));
	    double slope = tempcontrol->getSlope();
		mqtt.publish(top_topic+"/slope", String(slope));
	}
#endif
}
#define TFT_CS 15
#define TFT_DC 2
unsigned blink_now = 1000;

void setup() {

  #ifdef __ESP8266__
  
	int id = ESP.getChipId();
#else
 uint32_t id = 0;
  for(int i=0; i<17; i=i+8) {
    id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }

 
#endif
	switch(id) {
  case 0x6a5444: // olimex esp32-evb
    led_pin = 0;
    light_pin = 32;
    pinMode(33, OUTPUT);
    digitalWrite(33, LOW);
    defname="chickenbarn";
    break;
    
	case 0xc6e096:
		// Adafruit feather with TFT featherwing
		break;
	case 0xf569ca:
		// Adafruit feather with TFT featherwing
		break;

	case LINKNODE_R4:
		light_pin = LIGHT_PIN_R4;
    defname="drivewaytimer";
		led_pin = 2;
		break;
#ifdef ARDUINO_ESP8266_WEMOS_D1MINI

	case 0x4e4ea6:
		oled = new Adafruit_SSD1306(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
		oled->begin(SSD1306_SWITCHCAPVCC);  // Switch OLED
		oled->display();
		temp_dev = AbstractTemp::type_si7021;
		//light_pin = 4;
		led_pin=2;
		break;
  case 0x4e49e1:
    // wemos mini d1 w/ir
    led_pin=2;
    tempcontrol = new TempControl(MySerial, D0);
    tempcontrol->setTemp(80);
    subscribe = "sensor/suntimer23-4e4c92/temp\0sensor/adafruit/set_temp\0";
    //subscribe = "sensor/suntimer23-4e4ea6/temp\0sensor/adafruit/set_temp\0";
    break;
#endif

	case 0x4e4dc3:
		oled = new Adafruit_SSD1306(0);
		oled->begin(SSD1306_SWITCHCAPVCC,0x3d,false);  // Switch OLED

		oled->display();
		temp_dev = AbstractTemp::type_si7021;
		light_pin = 0;
		led_pin=2;
		break;
	case 0x4e4f7a:
	case 0x4e4c92:
	case 0x4e4991:
		temp_dev = AbstractTemp::type_si7021;
		light_pin = 0;
	case 0x4e4a36: // wemos;
	case 0x4e49dd:
	case 0x4e4dda:
	case 0x4e4c9b:
		led_pin=2;
		break;
	case 0x5a7d95: // nodemcu bare board
		led_pin=2;
		break;
	case 0x152669:
		temp_dev = AbstractTemp::type_MCP9808;

#ifdef TFT
		tft = new Adafruit_ILI9341(TFT_CS, TFT_DC);
#endif    
#ifdef LEDDISP
		display = new TM1638(14,12, 13);
#endif
		{
			int oled_reset_pin = 4;
			oled = new Adafruit_SSD1306(0);
			oled->begin(0, 0x3d,false);  // Switch OLED
			oled->display();
		}
		led_pin=2;
		light_pin=0;
		break;

  case 0xf0a449: // adafruit ESP-12 Huzzah
    led_pin=2;
    light_pin = 0;
    break;
    
	case 0x007a5e: // adafruit ESP-12 Huzzah breakout
		led_pin = 2;
		light_pin = 0;
		break;

		/*	case 0xc6e096: // adafruit ESP-12 Feather
		led_pin = 2;
		light_pin=14;
	    temp_dev = AbstractTemp::type_MCP9808;
		deepSleep=1;
		break;
	case 0xf569ca: // Olimex-EVB (not drivewaytimer)
		break;
		 */
  //case 0xf569ca: // Olimex-EVB (not drivewaytimer)
  //  break;
	case 0xf56977: // Olimex evb
	case 0x8e2c8c: // Olimex evb
		//deepSleep=1;
		temp_dev = AbstractTemp::type_DHT11;
		break;
  // sonoff device (ESP8285)
  case 0xe45a8f:
  case 0xe4f027:
  case 0xd3e7f0:
     light_pin=12;
     led_pin=13;
     subscribe = "sensor/adafruit/setSwitch\0";
     break;
	}
	if (id == LINKNODE_R4) light_pin = LIGHT_PIN_R4;

	pinMode(LIGHT_PIN, OUTPUT);
	digitalWrite(LIGHT_PIN, LOW);

  char tmp[30];
  sprintf(tmp, defname, id);
  ArduinoOTA.setHostname(tmp);
  mdns_hostname_set(tmp);

  Serial.begin(115200);
  Serial.print("v1.1 Connecting to station ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

	configTime(TIMEZONE * 3600, 3600, "pool.ntp.org", "time.nist.gov");
	//  timeClient.begin();

	Syslog("SunTimer Ver:" VERSION " startup");

	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	// ArduinoOTA.setHostname("myesp8266");

	// No authentication by default
	// ArduinoOTA.setPassword((const char *)"123");

	ArduinoOTA.onStart([]() {
		MySerial.println("Start");
		//lights(OFF);
		updating = 1;
	});
	ArduinoOTA.onEnd([]() {
		MySerial.println("\nEnd");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		MySerial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		MySerial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) MySerial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) MySerial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) MySerial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) MySerial.println("Receive Failed");
		else if (error == OTA_END_ERROR) MySerial.println("End Failed");
	});
	topic += tmp;
	top_topic = topic;
	topic += "/light";
	ArduinoOTA.begin();


	http_server.on("/light_on", []() {
		lights(ON);
		stats_page();
	});
	http_server.on("/identify", []() {
		blink_now = 100;
	});
	http_server.on("/light_off", []() {
		lights(OFF);
		stats_page();
	});
	http_server.on("/update", []() {
		//httpUpdate();
		stats_page();
	});
  http_server.on("/sleep", []() {
    //httpUpdate();
    sleep_enable ^= 1;
    stats_page();
  });
	http_server.on("/", []() {
		stats_page();
	});

	//filemgrSetup(&http_server);
	http_server.begin();
	MySerial.println("HTTP server started");
	//udp.begin(514);
	MDNS.addService("http", "tcp", 80);
	if (LED_BLINK)
  	pinMode(LED_BLINK, OUTPUT);

	MySerial.println("HTTP server started");
#ifdef TEMP
  atemp = new AbstractTemp(temp_dev);
	atemp->begin(Serial);
#endif
	if (display) {
#ifdef TFT
		MySerial.println("TFT started 11");
		tft->begin();
		MySerial.println("TFT started 22");

		tft->fillScreen(ILI9341_BLUE);
#endif
	} else {
		MySerial.println("ID: " + String(id,HEX));
	}
	mqtt.start(syslogServer, 1883, tmp);
	if (subscribe) {
		char *cur = subscribe;
		mqtt.setcallback(mqtt_callback);
		do {
			mqtt.subscribe(cur);
			Syslog(String("Subscribe: ")+ cur);
			cur += strlen(cur)+1;
		} while (*cur);
	}

	int nDevices = 0;
	int address;
	int error;
	for(address = 1; address < 127; address++ )
	{
		// The i2c_scanner uses the return value of
		// the Write.endTransmisstion to see if
		// a device did acknowledge to the address.
		Wire.beginTransmission(address);
		error = Wire.endTransmission();

		if (error == 0)
		{
			MySerial.print("I2C device found at address 0x");
			if (address<16)
				MySerial.print("0");
			MySerial.print(address,HEX);
			MySerial.println("  !");

			nDevices++;
		}
		else if (error==4)
		{
			MySerial.print("Unknown error at address 0x");
			if (address<16)
				MySerial.print("0");
			MySerial.println(address,HEX);
		}
	}
	if (nDevices == 0)
		MySerial.println("No I2C devices found\n");
	else
		MySerial.println("done\n");

}

void stats_page()
{
	String page = webPage;
	time_t t = time(nullptr);

	page += "<html>";
	page += "<style>table, th, td { border: 1px solid black; border-collapse: collapse; }</style>";
	page += "<body><table>";
	page += "SunTimer Version ";
	page += VERSION;
	page += "<br/><tr><td>RSSI</td><td>";
	page += WiFi.RSSI();
	page += "</td></tr>";
	page += "<tr><td>host</td><td>";
	page += ArduinoOTA.getHostname();
	page += "</td></tr>";

	page += "<tr><td>memory</td><td>";
	page += ESP.getFreeHeap();
	page += "</td></tr>";
	page += "<tr><td>free sketch</td><td>";
	page += ESP.getFreeSketchSpace();
	page += "</td></tr>";
	page += "<tr><td>cur sketch</td><td>";
	page += ESP.getSketchSize();
	page += "</td></tr>";
	page += "<tr><td>time</td><td>";
	page += asctime(localtime(&t));
	page += "</td></tr>";
	page += "<tr><td>dawn</td><td>";
	t = sunRise.getEvent();
	page += asctime(localtime(&t));
	page += "</td></tr>";
	page += "<tr><td>dusk</td><td>";
	t = sunSet.getEvent();
	page += asctime(localtime(&t));
	page += "</td></tr>";
	page += "<tr><td>hour</td><td>";
	page += getHour();
	page += "</td></tr>";
	page += "<tr><td>light</td><td>";
	page += digitalRead(LIGHT_PIN);
	page += "</td></tr>";
	page += "<tr><td>poll</td><td>";
	page += millis()-last_poll;
	page += "</td></tr>";
  page += "<tr><td>sleep</td><td>";
  page += sleep_enable;
  page += "</td></tr>";
	page += "<tr><td>light_on</td><td>";
	page += light_on;
	page += "</td></tr>";
	page += "<tr><td>state</td><td>";
	page += state;
	page += "</td></tr>";
	page += "<tr><td>time</td><td>";
	page += time(nullptr);
	page += "</td></tr>";
  page += "<tr><td>up-time</td><td>";
  time_t diff;
  int days;
  int hours;
  hours = (time(nullptr) - uptime)/3600;
  days = hours/24;
  hours %= 24;
  page += days;
  page += " days ";
  page += hours;
  page += " hours";
  page += "</td></tr>";
	page += "</table></br>";

	if (light_on)
		page += "<input type=button value='Light Off' onmousedown=location.href='/light_off'><br/>";
	else
		page += "<input type=button value='Light On' onmousedown=location.href='/light_on'><br/>";
	page += "<input type=button value='Refresh' onmousedown=location.href='/'><br/>";
  page += "<input type=button value='sleep' onmousedown=location.href='/sleep'><br/>";

	page += "</body></html>";
	http_server.send(200, "text/html", page);

}

/*
 *
 *void handleOnOffAlarm()

{
  time_t t = time(nullptr);

   // every day at noon, calculate the next solar event
   if (noon.fired(t)) {
    sunRise.set(solar.sunRise(t+DAY));
    sunSet.set(solar.sunSet(t));
      noon.set(t+DAY);
   }
   if (sunRise.fired(t)) {
     lights(OFF);
       sunRise.set(solar.sunRise(t+DAY));
   }
   if (sunRise.fired(t)) {
     lights(ON);
       sunSet.set(solar.sunSet(t+DAY));
   }

}
 */
time_t nextTime(time_t now, int sunset)
{
	time_t cur = now-DAY;
	time_t nxt;
	do {
		nxt = suntime(&Serial, cur, LAT, LON, sunset, TIMEZONE);
		cur += HOUR*8;
	} while (nxt < now);
	return nxt;
}
#define POLL_MINUTE (60*1000)
void handleOnOff()
{
	int mode=OFF;
	if ((millis() - last_poll) > POLL_MINUTE) {
		time_t now = time(nullptr);
    time_t uphrs = (now-uptime)/3600;

		Syslog("poll");
    lowpower();
		switch(state) {
		case STATE_WAIT_EPOCH:
			if (time(nullptr) > MYEPOCH) {
				state = STATE_HAVE_TIME;

        if (uptime == 0) uptime = now;
				time_t rise = nextTime(now, false);
				time_t set = nextTime(now, true);
				sunRise.setEvent(rise);
				sunSet.setEvent(set);

				if (rise > set) {
					Syslog("Start: Sunrise, light off");
					lights(OFF);
				} else {
					Syslog("Start: Sunset, light on");
					lights(ON);
				}
			}
			break;

		case STATE_HAVE_TIME:
			if (sunRise.isHaveEvent(now)) {
				Syslog("Sunrise, light off");
				mode = OFF;
				time_t rise = nextTime(now, false);
				sunRise.setEvent(rise);
			} else
				if (sunSet.isHaveEvent(time(nullptr))) {
					Syslog("Sunset, light on");
					mode = ON;
					time_t set = nextTime(now, true);
					sunSet.setEvent(set);
				} else {
					// don't "flash" the lights,  just keep the mode until we are stable (now)
					lights(mode);
					state = STATE_STABLE;
				}
			break;

		case STATE_STABLE:
			if (sunRise.isHaveEvent(now)) {
				Syslog("Sunrise, light off");
				lights(OFF);
				time_t rise = nextTime(now, false);
				sunRise.setEvent(rise);
			}
			if (sunSet.isHaveEvent(time(nullptr))) {
				Syslog("Sunset, light on");
				lights(ON );
				time_t set = nextTime(now, true);
				sunSet.setEvent(set);
			}
      mqtt.publish(top_topic+"/up", String(uphrs));
      MySerial.println("Up: " + String(uphrs));

			break;
		}
#ifdef TEMP
		if (atemp->haveTemp()) {
			float f = atemp->GetTemp();
			mqtt.publish(top_topic+"/temp", String(f));
			MySerial.println("Temp: " + String(f) + "F");
			if (display) {
#ifdef TFT
				tft->setCursor(0,0);
				tft->setTextColor(ILI9341_WHITE);
				tft->setTextSize(2);
				tft->println(String(f)+"F");
#endif
#if defined(LED_DISP) || defined(TFT)
				display->setDisplayToString(String(f)+"F");
#endif
			}
			if (oled) {
				struct tm *lt;
				char tmp[30];

				lt = localtime(&now);
				// Clear the buffer.
				oled->clearDisplay();
				oled->setTextSize(1);
				oled->setTextColor(WHITE);
				oled->setCursor(0,0);
				oled->print("Temp:");
				oled->setTextSize(2);
				oled->println(String(f)+"F");
				oled->setTextSize(1);
				oled->print("Time:");
				oled->setTextSize(2);
				sprintf(tmp, "%d:%02d", lt->tm_hour, lt->tm_min);
				oled->println(tmp);


			}

		}
		if (atemp->haveHumidity()) {
			float h = atemp->GetHumidity();

			mqtt.publish(top_topic+"/humidity", String(h));
			MySerial.println("Humidity: " + String(h));
			if (oled) {
				char tmp[30];
				oled->setTextSize(1);
				oled->print("Humi:");
				oled->setTextSize(2);
				sprintf(tmp, "%f", h);
				oled->println(tmp);
			}
		}
#endif
		if (oled)
			oled->display();

		last_poll = millis();

		if (deepSleep--==0) {
			Shutdown();
		}
		if (WiFi.status() !=  WL_CONNECTED) {
			WiFi.begin(ssid, password);
		}
	}
}

unsigned blink_decay=10;
unsigned blink_period=1000;

static unsigned long led_last_poll = 0;
static unsigned led_state = 0;
static unsigned states[] = {2000,250, 250, 250};

void blinkLED()
{
	unsigned long now = millis();
	int blinkevery;

	if (WiFi.status() ==  WL_CONNECTED)
		blinkevery=states[led_state%4];
	else
		blinkevery=500;

	if ((now - led_last_poll) > blinkevery) {
		++led_state;
		digitalWrite(LED_BLINK, led_state%2?LOW:HIGH);
		led_last_poll = now;
	}
}
void loop() {
	ArduinoOTA.handle();
	if (!updating) {
		http_server.handleClient();
		handleOnOff();
		blinkLED();
		mqtt.poll();
	}
}
