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

#define DEBUG
#define INTERVAL_TIME   10
#define INACTIVE_TIME   300
#define THRESHOLD       750

class config: public Configuration {
public:
  char ssid[33];
  char password[33];
  char hostname[17];
  char mqtt_server[33];
  int interval_time = INTERVAL_TIME;
  int inactive_time = INACTIVE_TIME;
  int threshold = THRESHOLD;
  int switch_idx = -1;
  int pir_idx = -1;
  int on_delay = 0;
  int off_delay = 0;

  void configure(JsonObject &o);
} cfg;

void config::configure(JsonObject &o) {
  strncpy(ssid, o[F("ssid")] | "", sizeof(ssid));
  strncpy(password, o[F("password")] | "", sizeof(password));
  strncpy(hostname, o[F("hostname")] | "", sizeof(hostname));
  strncpy(mqtt_server, o[F("mqtt_server")] | "", sizeof(mqtt_server));
  interval_time = 1000 * (long)o[F("interval_time")];
  inactive_time = 1000 * (long)o[F("inactive_time")];
  threshold = (int)o[F("threshold")];
  switch_idx = (int)o[F("switch_idx")];
  pir_idx = (int)o[F("pir_idx")];
  on_delay = (int)o[F("on_delay")];
  off_delay = (int)o[F("off_delay")];
}

#define PIR   D2
#define PIR_LED D4
#define POWER D5
#define HZ    4
#define SAMPLES (15*HZ)

#define CMND  "cmnd/twilight/"
#define STAT  "stat/twilight/"
#define PWR   "power"
#define INTVL "intvl"
#define LIGHT "light"
#define TIME  "time"
#define STAT_PWR  STAT PWR
#define STAT_PIR  STAT "pir"
#define STAT_INTVL  STAT INTVL
#define STAT_LIGHT  STAT LIGHT
#define STAT_TIME  STAT TIME
#define CMND_ALL  CMND "+"
#define CMND_PWR  CMND PWR
#define CMND_INTVL CMND INTVL
#define CMND_LIGHT CMND LIGHT
#define CMND_TIME CMND TIME
#define TO_DOMOTICZ "domoticz/in"
#define FROM_DOMOTICZ "domoticz/out"

static int samples[SAMPLES], pos, smoothed;
static long total;
static bool on;
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

static bool power(bool onoff) {
  if (on == onoff)
    return false;
  on = onoff;
#ifdef DEBUG
  Serial.printf("power: %d\n", on);
#endif
  if (on) {
    last_activity = millis();
    for (int i = 0; i <= PWMRANGE; i++) {
      analogWrite(POWER, i);
      delay(cfg.on_delay);
    }
  } else
    for (int i = PWMRANGE; i >= 0; i--) {
      analogWrite(POWER, i);
      if (digitalRead(PIR))
        return false;
      delay(cfg.off_delay);
    }
  return true;
}

static int sampleLight() {
  static long last_sample;
  static int n;
  int light = analogRead(A0);

  total += light - samples[pos];
  samples[pos++] = light;
  if (n < SAMPLES)
    n++;
  if (pos == SAMPLES) pos = 0;
  smoothed = (int)(total / n);
  
  long now = millis();
  if (now - last_sample > cfg.interval_time) {
    last_sample = now;
    pub(STAT_LIGHT, smoothed);
#ifdef DEBUG
    Serial.printf("light=%d %d\r\n", light, smoothed);
#endif
  }
  return smoothed;
}

static void flash(int ms, int n) {
  for (int i = 0; i < n; i++) {
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
      if (strcmp(topic, CMND_PWR) == 0 && power(*payload == '1')) {
        pub(STAT_PWR, on);
        domoticz_pub(cfg.switch_idx, on);
        return;
      }
#ifdef FROM_DOMOTICZ
      if (strcmp(topic, FROM_DOMOTICZ) == 0) {
        DynamicJsonBuffer buf(JSON_OBJECT_SIZE(14) + 230);
        JsonObject& root = buf.parseObject(payload);
        if (root["idx"] == cfg.switch_idx && power(root["nvalue"] == 1)) {
          pub(STAT_PWR, on);
          return;
        }
      }
#endif
    });

    flash(250, 2);
  }
}

static void mqtt_connect() {
#ifdef DEBUG
  Serial.print(F("Attempting MQTT connection to: "));
  Serial.print(cfg.mqtt_server);
#endif
  if (mqtt_client.connect(cfg.hostname)) {
#ifdef DEBUG
    Serial.println(F(" connected"));
#endif
    mqtt_client.subscribe(CMND_ALL);
#ifdef FROM_DOMOTICZ
    mqtt_client.subscribe(FROM_DOMOTICZ);
#endif
  } else {
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
  long now = millis();
  int pir = digitalRead(PIR);
  if (last_pir != pir) {
    digitalWrite(PIR_LED, !pir);
    last_pir = pir;
    pub(STAT_PIR, pir);
    domoticz_pub(cfg.pir_idx, pir);
#ifdef DEBUG
    Serial.printf("%d pir=%d\r\n", now, pir);
#endif
  }
  int light = sampleLight();
  if (pir) {
    last_activity = now;
    if (light > cfg.threshold && power(true)) {
      pub(STAT_PWR, on);
      domoticz_pub(cfg.switch_idx, on);
    }
  } else if ((now - last_activity) > cfg.inactive_time && power(false)) {
    pub(STAT_PWR, on);
    domoticz_pub(cfg.switch_idx, on);
  }

  delay(1000 / HZ);
}
