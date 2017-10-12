
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "filemgr.h"
#include "sunMoon.h"

#include "/kghome.h"

const char* defname = "suntimer-%06x";
IPAddress syslogServer(192, 168, 1, 199);


//todo: get lat/lon from net : http://ip-api.com/json
#define LAT 27.9158
#define LON -82.229
#define TIMEZONE -5
#define MYEPOCH 1498500000
#define DAY (3600*24)
#define POLL_TIME (10*1000)



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
#define MAX_SRV_CLIENTS 5
WiFiClient serverClients[MAX_SRV_CLIENTS];
unsigned long a=1;
TimeEvent sunRise;
TimeEvent sunSet;

#define LIGHT_PIN 5

//WiFiUDP ntpUDP;
//NTPClient timeClient(ntpUDP, -5);
WiFiUDP udp;
void Syslog(String msgtosend)
{
  unsigned int msg_length = msgtosend.length();
  byte* p = (byte*)malloc(msg_length);
  memcpy(p, (char*) msgtosend.c_str(), msg_length);

  udp.beginPacket(syslogServer, 514);
  udp.write(ArduinoOTA.getHostname().c_str());
  udp.write(p, msg_length);
  udp.endPacket();
  free(p);
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
void setup() {
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

  char tmp[15];
  int id = ESP.getChipId();

  sprintf(tmp, defname, id);

  ArduinoOTA.setHostname(tmp);
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  http_server.on("/light_on", []() {
    lights(ON);
    stats_page();
  });
  http_server.on("/light_off", []() {
    lights(OFF);
    stats_page();
  });
  http_server.on("/", []() {
    lights(OFF);
    stats_page();
  });

  filemgrSetup(&http_server);
  http_server.begin();
  Serial.println("HTTP server started");

  MDNS.addService("http", "tcp", 80);

}

void stats_page()
{
  String page = webPage;
  time_t t = time(nullptr);

  page += "<html>";
  page += "<style>table, th, td { border: 1px solid black; border-collapse: collapse; }</style>";
  page += "<body><table>";
  page += "Ver 0.14</br>";
  page += "<tr><td>RSSI</td><td>";
  page += WiFi.RSSI();
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
  page += "<input type=button value='Light On' onmousedown=location.href='/light_on'><br/>";
  page += "<input type=button value='Light Off' onmousedown=location.href='/light_off'><br/>";
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
    cur += 12;
  } while (nxt < now);
  return nxt;
}
void handleOnOff()
{
  if ((millis() - last_poll) > POLL_TIME) {
    time_t now = time(nullptr);


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
unsigned long last_poll;
#define POLL_TIME 1*1000
unsigned state = 0;
void blinkLED()
{
  unsigned long now = millis();
  int blinkevery;

  if (WiFi.status() ==  WL_CONNECTED)
    blinkevery=1*1000;
  else
    blinkevery=1000;

  if ((now - last_poll) > blinkevery) {
    state ^= HIGH;
    digitalWrite(LED_BLINK, state);
    last_poll = now;
  }
}

void loop() {
  ArduinoOTA.handle();
  //timeClient.update();
  http_server.handleClient();
  handleOnOff();
  blinkLED();
}


