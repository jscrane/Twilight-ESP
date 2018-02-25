#include <ArduinoJson.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>

#include "Configuration.h"

MDNSResponder mdns;
WiFiClient wifiClient;
PubSubClient mqtt_client(wifiClient);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

#define INTERVAL_TIME   10
#define INACTIVE_TIME   300
#define THRESHOLD       750

class config: public Configuration {
public:
  char ssid[33];
  char password[33];
  char hostname[17];
  char mqtt_server[33];
  long interval_time = INTERVAL_TIME;
  long inactive_time = INACTIVE_TIME;
  unsigned threshold = THRESHOLD;
  int switch_idx;
  int pir_idx;
  unsigned on_delay;
  unsigned off_delay;
  unsigned on_bright;
  unsigned off_bright;

  void configure(JsonObject &o);
} cfg;

void config::configure(JsonObject &o) {
  strlcpy(ssid, o[F("ssid")] | "", sizeof(ssid));
  strlcpy(password, o[F("password")] | "", sizeof(password));
  strlcpy(hostname, o[F("hostname")] | "", sizeof(hostname));
  strlcpy(mqtt_server, o[F("mqtt_server")] | "", sizeof(mqtt_server));
  interval_time = 1000 * (long)o[F("interval_time")];
  inactive_time = 1000 * (long)o[F("inactive_time")];
  threshold = o[F("threshold")];
  switch_idx = o[F("switch_idx")];
  pir_idx = o[F("pir_idx")];
  on_delay = o[F("on_delay")];
  off_delay = o[F("off_delay")];
  on_bright = o[F("on_bright")];
  off_bright = o[F("off_bright")];
}

#define PIR   D2
#define PIR_LED D4
#define POWER D1
#define HZ    4
#define SAMPLES (15*HZ)

#define CMND  "cmnd/twilight/"
#define STAT  "stat/twilight/"
#define PWR   "power"
#define LIGHT "light"
#define STAT_PWR  STAT PWR
#define STAT_PIR  STAT "pir"
#define STAT_LIGHT  STAT LIGHT
#define CMND_ALL  CMND "+"
#define CMND_PWR  CMND PWR
#define CMND_LIGHT CMND LIGHT
#define TO_DOMOTICZ "domoticz/in"
#define FROM_DOMOTICZ "domoticz/out"

enum State {
  OFF = 0,
  ON,
  AUTO_OFF,
  AUTO_ON,
  MQTT_OFF,
  MQTT_ON,
  DOMOTICZ_OFF,
  DOMOTICZ_ON
};
static State state;

inline bool isOff() {
  return state == OFF || state == AUTO_OFF || state == MQTT_OFF || state == DOMOTICZ_OFF;
}

inline bool isOn() {
  return state == ON || state == AUTO_ON || state == MQTT_ON || state == DOMOTICZ_ON;
}

static long last_activity;
static bool connected;

static void pub(const char *topic, int val) {
  char msg[16];
  snprintf(msg, sizeof(msg), "%d", val);
  mqtt_client.publish(topic, msg);
}

static void domoticz_pub(int idx, int val) {
  if (idx != -1) {
    char msg[64];
    snprintf(msg, sizeof(msg), "{\"idx\":%d,\"nvalue\":%d,\"svalue\":\"\"}", idx, val);
    mqtt_client.publish(TO_DOMOTICZ, msg);
  }
}

static int sampleLight() {
  static int samples[SAMPLES], pos;
  static long total;
  static long last_sample;
  static int n;
  int light = analogRead(A0);

  total += light - samples[pos];
  samples[pos++] = light;
  if (n < SAMPLES)
    n++;
  if (pos == SAMPLES) pos = 0;
  int smoothed = (int)(total / n);
  
  long now = millis();
  if (now - last_sample > cfg.interval_time) {
    last_sample = now;
    pub(STAT_LIGHT, smoothed);
  }
  return smoothed;
}

static void flash(int ms, int n) {
  for (int i = 0; i < n; i++) {
    bool on = digitalRead(POWER);
    digitalWrite(POWER, !on);
    delay(ms);
    digitalWrite(POWER, on);
    delay(ms);      
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("Booting!"));
  
  pinMode(PIR, INPUT);
  pinMode(POWER, OUTPUT);
  pinMode(PIR_LED, OUTPUT);

  digitalWrite(POWER, LOW);
  flash(500, 2);

  bool result = SPIFFS.begin();
  if (!result) {
    Serial.print(F("SPIFFS: "));
    Serial.println(result);
    return;
  }

  if (!cfg.read_file("/config.json")) {
    Serial.print(F("config!"));
    return;
  }

  Serial.print(F("Hostname: "));
  Serial.println(cfg.hostname);
  Serial.print(F("MQTT Server: "));
  Serial.println(cfg.mqtt_server);
  Serial.print(F("Threshold: "));
  Serial.println(cfg.threshold);
  Serial.print(F("Interval time: "));
  Serial.println(cfg.interval_time);
  Serial.print(F("Inactive time: "));
  Serial.println(cfg.inactive_time);
  Serial.print(F("Switch idx: "));
  Serial.println(cfg.switch_idx);
  Serial.print(F("PIR idx: "));
  Serial.println(cfg.pir_idx);
  Serial.print(F("On delay: "));
  Serial.println(cfg.on_delay);
  Serial.print(F("Off delay: "));
  Serial.println(cfg.off_delay);
  Serial.print(F("On bright: "));
  Serial.println(cfg.on_bright);
  Serial.print(F("Off bright: "));
  Serial.println(cfg.off_bright);

  WiFi.mode(WIFI_STA);
  WiFi.hostname(cfg.hostname);
  if (*cfg.ssid) {
    WiFi.begin(cfg.ssid, cfg.password);
    for (int i = 0; i < 120 && WiFi.status() != WL_CONNECTED; i++) {
      flash(250, 1);
      Serial.print('.');
    }
    Serial.println();
    connected = WiFi.status() == WL_CONNECTED;
  }

  server.on("/config", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      String body = server.arg("plain");
      File f = SPIFFS.open("/config.json", "w");
      f.print(body);
      f.close();
      ESP.restart();
    } else
      server.send(400, "text/plain", "No body!");
  });
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/config", SPIFFS, "/config.json");
  server.serveStatic("/js/transparency.min.js", SPIFFS, "/transparency.min.js");

  httpUpdater.setup(&server);
  server.begin();

  if (mdns.begin(cfg.hostname, WiFi.localIP())) {
    Serial.println(F("mDNS started"));
    mdns.addService("http", "tcp", 80);
  } else
    Serial.println(F("Error starting MDNS"));

  if (!connected) {
    WiFi.softAP(cfg.hostname);
    Serial.print(F("Connect to SSID: "));
    Serial.print(cfg.hostname);
    Serial.println(F(" and URL http://192.168.4.1 to configure WIFI"));
  } else {
    Serial.print(F("Connected to "));
    Serial.println(cfg.ssid);
    Serial.println(WiFi.localIP());
  
    mqtt_client.setServer(cfg.mqtt_server, 1883);
    mqtt_client.setCallback([](char *topic, byte *payload, unsigned int length) {
      if (strcmp(topic, CMND_PWR) == 0) {
        last_activity = millis();
        bool cmdOn = (*payload == '1');
        if (cmdOn && isOff())
          state = MQTT_ON;
        else if (!cmdOn && isOn())
          state = MQTT_OFF;
      } else if (strcmp(topic, FROM_DOMOTICZ) == 0) {
        DynamicJsonBuffer buf(JSON_OBJECT_SIZE(14) + 230);
        JsonObject& root = buf.parseObject(payload);
        if (root[F("idx")] == cfg.switch_idx) {
          last_activity = millis();
          int v = (int)root[F("nvalue")];
          if (v == 1 && isOff())
            state = DOMOTICZ_ON;
          else if (v == 0 && isOn())
            state = DOMOTICZ_OFF;
        }
      }
    });

    flash(250, 2);
    analogWrite(POWER, cfg.off_bright);
  }
}

static void mqtt_connect() {
  if (mqtt_client.connect(cfg.hostname)) {
    mqtt_client.subscribe(CMND_ALL);
    mqtt_client.subscribe(FROM_DOMOTICZ);
  } else {
    Serial.print(F("MQTT connection to: "));
    Serial.print(cfg.mqtt_server);
    Serial.print(F(" failed, rc="));
    Serial.print(mqtt_client.state());
  }
}

void loop() {
  mdns.update();
  server.handleClient();

  if (!connected) {
    flash(1000, 1);
    return;
  }
  
  if (!mqtt_client.connected())
    mqtt_connect();

  if (mqtt_client.connected())
    mqtt_client.loop();
    
  static int last_pir;
  static unsigned fade;
  static State last_state;
  long now = millis();
  int light = sampleLight();
  int pir = digitalRead(PIR);
  if (last_pir != pir) {
    digitalWrite(PIR_LED, !pir);
    last_pir = pir;
    pub(STAT_PIR, pir);
    domoticz_pub(cfg.pir_idx, pir);
    if (pir) {
      last_activity = now;
      if (light > cfg.threshold && isOff())
        state = AUTO_ON;
    }
  }
  unsigned d = 1000 / HZ;
  switch (state) {
  case OFF:
    break;
  case AUTO_OFF:
  case MQTT_OFF:
  case DOMOTICZ_OFF:
    if (fade == cfg.off_bright) {
      domoticz_pub(cfg.switch_idx, false);
      state = OFF;
    } else {
      fade--;
      analogWrite(POWER, fade);
      d = cfg.off_delay;
    }
    break;
  case ON:
    if ((now - last_activity) > cfg.inactive_time)
      state = AUTO_OFF;
    break;
  case AUTO_ON:
  case MQTT_ON:
  case DOMOTICZ_ON:
    if (fade == cfg.on_bright) {
      domoticz_pub(cfg.switch_idx, true);
      state = ON;
    } else {
      fade++;
      analogWrite(POWER, fade);
      d = cfg.on_delay;
    }
    break;
  }
  if (state != last_state) {
      pub(STAT_PWR, state);
      last_state = state;
  }
  delay(d);
}
