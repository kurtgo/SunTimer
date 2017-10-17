#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#define LOG MySerial.println
#include <ESP8266MQTTClient.h>
#include <ESP8266WiFi.h>
MQTTClient mqtt;

#include <time.h>
#include "filemgr.h"
#include "sunMoon.h"
#define LOG Syslog
#include "/kghome.h"

#ifndef ENABLE_PRINT
// disable Serial output
#define Serial MySerial

IPAddress syslogServer(192, 168, 1, 199);
WiFiUDP udp;

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
        udp.write(buffer, 1024-left);
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
#endif

#define VERSION "0.18"
#define LED_BLINK led_pin

#define ESP_1_ONBOARD_LED 1
#define ESP_12_ONBOARD_LED 2

int led_pin = ESP_1_ONBOARD_LED;

const char* defname = "suntimer-%06x";


//todo: get lat/lon from net : http://ip-api.com/json
#define LAT 27.9158
#define LON -82.229
#define TIMEZONE -5
#define MYEPOCH 1498500000
#define HOUR 3600
#define DAY (HOUR*24)



static long last_poll = 0;
static enum {STATE_WAIT_EPOCH, STATE_HAVE_TIME} state = STATE_WAIT_EPOCH;


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
ESP8266WebServer http_server(80);
String webPage = "";
String topic = "sensor/";
#define MAX_SRV_CLIENTS 5
WiFiClient serverClients[MAX_SRV_CLIENTS];
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

void startmqtt()
{
    //topic, data, data is continuing
  mqtt.onData([](String topic, String data, bool cont) {
    Serial.printf("Data received, topic: %s, data: %s\r\n", topic.c_str(), data.c_str());
    mqtt.unSubscribe("/qos0");
  });

  mqtt.onSubscribe([](int sub_id) {
    Serial.printf("Subscribe topic id: %d ok\r\n", sub_id);
    mqtt.publish("/qos0", "qos0", 0, 0);
  });
  mqtt.onConnect([]() {
    Serial.printf("MQTT: Connected\r\n");
//    Serial.printf("Subscribe id: %d\r\n", mqtt.subscribe("/qos0", 0));
//    mqtt.subscribe("/qos1", 1);
//    mqtt.subscribe("/qos2", 2);
  });

  mqtt.begin("mqtt://192.168.1.199:1883");
//  mqtt.begin("mqtt://test.mosquitto.org:1883", {.lwtTopic = "hello", .lwtMsg = "offline", .lwtQos = 0, .lwtRetain = 0});
//  mqtt.begin("mqtt://user:pass@mosquito.org:1883");
//  mqtt.begin("mqtt://user:pass@mosquito.org:1883#clientId");

}
enum MODE {OFF=0, ON=1, TOGGLE=2};
int light_on = OFF;

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
  Serial.print("Publish ");
  Serial.print(topic);
  Serial.print(" ");
  Serial.println(light_on);
  mqtt.publish(topic, String(light_on), 0, 1);
  digitalWrite(LIGHT_PIN, light_on);
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

int getHour()
{
  time_t t = time(nullptr);
  return localtime(&t)->tm_hour;

}
unsigned blink_now = 1000;
void setup() {

  int id = ESP.getChipId();

  switch(id) {
  case LINKNODE_R4:
    light_pin = LIGHT_PIN_R4;
    led_pin = 2;
    break;
  case 0x152669:
  case 0x007a5e: // adafruit ESP-12 Huzzah
    led_pin = 2;
    break;
  case 0xc6e096: // adafruit ESP-12 Feather
    led_pin = 2;
    break;
  }
  if (id == LINKNODE_R4) light_pin = LIGHT_PIN_R4;
  
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);

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

  configTime(TIMEZONE * 3600, 0, "pool.ntp.org", "time.nist.gov");
  //  timeClient.begin();

  Syslog("SunTimer Ver:" VERSION " startup");
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
    lights(OFF);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  char tmp[30];

  sprintf(tmp, defname, id);
 
  ArduinoOTA.setHostname(tmp);
  topic += tmp;
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
  http_server.on("/", []() {
    lights(OFF);
    stats_page();
  });

  //filemgrSetup(&http_server);
  http_server.begin();
  Serial.println("HTTP server started");
  //udp.begin(514);
  MDNS.addService("http", "tcp", 80);
  pinMode(LED_BLINK, OUTPUT);

  startmqtt();
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
  page += "<tr><td>light_on</td><td>";
  page += light_on;
  page += "</td></tr>";
  page += "<tr><td>state</td><td>";
  page += state;
  page += "</td></tr>";
  page += "<tr><td>time</td><td>";
  page += time(nullptr);
  page += "</td></tr>";
  page += "</table></br>";
  
  if (light_on) 
    page += "<input type=button value='Light Off' onmousedown=location.href='/light_off'><br/>";
  else 
    page += "<input type=button value='Light On' onmousedown=location.href='/light_on'><br/>";
  page += "<input type=button value='Refresh' onmousedown=location.href='/'><br/>";
  page += "<input type=button value='Identify' onmousedown=location.href='/identify'><br/>";

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
    nxt = suntime(cur, LAT, LON, sunset, TIMEZONE);
    cur += HOUR*8;
  } while (nxt < now);
  return nxt;
}
#define POLL_MINUTE (60*1000)
void handleOnOff()
{
  if ((millis() - last_poll) > POLL_MINUTE) {
    time_t now = time(nullptr);

    Syslog("poll");
    switch(state) {
    case STATE_WAIT_EPOCH:
      if (time(nullptr) > MYEPOCH) {
        state = STATE_HAVE_TIME;
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
      break;
    }
    last_poll = millis();
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
  http_server.handleClient();
  handleOnOff();
  blinkLED();
  mqtt.handle();
}


