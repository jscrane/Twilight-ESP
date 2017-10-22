#include <ArduinoJson.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "Configuration.h"

WiFiClient wifiClient;
PubSubClient mqtt_client(wifiClient);
MDNSResponder mdns;

//#define DEBUG
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

  void configure(const char *key, const char *value);
} cfg;

void config::configure(const char *p, const char *q) {
    if (strcmp(p, "ssid") == 0)
      strncpy(ssid, q, sizeof(ssid));
    else if (strcmp(p, "password") == 0)
      strncpy(password, q, sizeof(password));
    else if (strcmp(p, "hostname") == 0)
      strncpy(hostname, q, sizeof(hostname));
    else if (strcmp(p, "mqtt_server") == 0)
      strncpy(mqtt_server, q, sizeof(mqtt_server));
    else if (strcmp(p, "interval_time") == 0)
      interval_time = atoi(q);
    else if (strcmp(p, "inactive_time") == 0)
      inactive_time = atoi(q);
    else if (strcmp(p, "threshold") == 0)
      threshold = atoi(q);
    else if (strcmp(p, "switch_idx") == 0)
      switch_idx = atoi(q);
    else if (strcmp(p, "pir_idx") == 0)
      pir_idx = atoi(q);
}

#define PIR   4
#define PIR_LED 2
#define POWER 5
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
//#define FROM_DOMOTICZ "domoticz/out"

static int samples[SAMPLES], pos, smoothed;
static long total;
static bool on;
static long last_activity;

static void pub(const char *topic, bool val) {
  mqtt_client.publish(topic, val? "1": "0");
}

static void pub(const char *topic, int val) {
  char msg[16];
  snprintf(msg, sizeof(msg), "%d", val);
  mqtt_client.publish(topic, msg);
}

static void domoticz_pub(int idx, int val) {
  char msg[64];
  snprintf(msg, sizeof(msg), "{\"idx\":%d,\"nvalue\":%d,\"svalue\":\"\"}", idx, val);
  mqtt_client.publish(TO_DOMOTICZ, msg);
}

static void power(bool onoff) {
  on = onoff;
  if (on)
    last_activity = millis();
  digitalWrite(POWER, on);
  pub(STAT_PWR, on);
  if (cfg.switch_idx != -1)
    domoticz_pub(cfg.switch_idx, onoff);
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
  if (now - last_sample > 1000 * cfg.interval_time) {
    last_sample = now;
    pub(STAT_LIGHT, smoothed);
#ifdef DEBUG
    Serial.printf("light=%d %d\r\n", light, smoothed);
#endif
  }
  return smoothed;
}

static char *as_chars(char *buf, int n, byte *payload, int length) {
  int i;
  for (i = 0; i < length && i < n; i++)
    buf[i] = (char)payload[i];
  buf[i == n? n-1: i] = 0;  
  return buf;
}

static void set_interval_time(byte *payload, int length) {
  char buf[32];
  cfg.configure("interval_time", as_chars(buf, sizeof(buf), payload, length));
  mqtt_client.publish(STAT_INTVL, buf);
}

static void set_threshold(byte *payload, int length) {
  char buf[32];
  cfg.configure("threshold", as_chars(buf, sizeof(buf), payload, length));
  mqtt_client.publish(STAT_LIGHT, buf);
}

static void set_time(byte *payload, int length) {
  char buf[32];
  cfg.configure("inactive_time", as_chars(buf, sizeof(buf), payload, length));
  mqtt_client.publish(STAT_TIME, buf);
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
  Serial.println("Booting!");
  
  pinMode(PIR, INPUT);
  pinMode(POWER, OUTPUT);
  pinMode(PIR_LED, OUTPUT);
  flash(250, 1);

  bool result = SPIFFS.begin();
  if (!result) {
    Serial.print("SPIFFS: ");
    Serial.println(result);
    return;
  }

  if (!cfg.read_file("/config.txt")) {
    Serial.print(F("config!"));
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid, cfg.password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(cfg.ssid);
  
  if (mdns.begin(cfg.hostname, WiFi.localIP()))
    Serial.println("mDNS started");

  ArduinoOTA.setHostname(cfg.hostname);
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
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
  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.printf("Hostname: %s\r\n", cfg.hostname);
  Serial.printf("MQTT Server: %s\r\n", cfg.mqtt_server);  
  Serial.printf("Threshold: %d\r\n", cfg.threshold);
  Serial.printf("Interval time: %d\r\n", cfg.interval_time);
  Serial.printf("Inactive time: %d\r\n", cfg.inactive_time);
  Serial.printf("Switch idx: %d\r\n", cfg.switch_idx);
  Serial.printf("PIR idx: %d\r\n", cfg.pir_idx);

  mqtt_client.setServer(cfg.mqtt_server, 1883);
  mqtt_client.setCallback([](char *topic, byte *payload, unsigned int length) {
#ifdef DEBUG
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i=0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
#endif
    if (strcmp(topic, CMND_PWR) == 0) {
      power(*payload == '1');
      return;      
    }
    if (strcmp(topic, CMND_INTVL) == 0)
      set_interval_time(payload, length);
    else if (strcmp(topic, CMND_LIGHT) == 0)
      set_threshold(payload, length);
    else if (strcmp(topic, CMND_TIME) == 0)
      set_time(payload, length);
#ifdef FROM_DOMOTICZ
    else if (strcmp(topic, FROM_DOMOTICZ) == 0) {
      const size_t bufferSize = JSON_OBJECT_SIZE(14) + 230;
      DynamicJsonBuffer buf(bufferSize);
      JsonObject& root = buf.parseObject(payload);
      if (root["idx"] == cfg.switch_idx)
        power(root["nvalue"] == 1);
    }
#endif
    flash(125, 1);
  });

  flash(250, 2);
  power(false);
}

static void mqtt_connect() {
#ifdef DEBUG
  Serial.print("Attempting MQTT connection...");
#endif
  if (mqtt_client.connect(cfg.hostname)) {
#ifdef DEBUG
    Serial.println("connected");
#endif
    mqtt_client.subscribe(CMND_ALL);
#ifdef FROM_DOMOTICZ
    mqtt_client.subscribe(FROM_DOMOTICZ);
#endif
  } else {
    Serial.print("failed, rc=");
    Serial.print(mqtt_client.state());
  }
}

void loop() {
  static int last_pir;
  
  if (!mqtt_client.connected())
    mqtt_connect();

  if (mqtt_client.connected())
    mqtt_client.loop();
    
  ArduinoOTA.handle();
  long now = millis();
  int pir = digitalRead(PIR);
  if (last_pir != pir) {
    digitalWrite(PIR_LED, !pir);
    last_pir = pir;
    pub(STAT_PIR, pir);
    if (cfg.pir_idx != -1)
      domoticz_pub(cfg.pir_idx, pir);
#ifdef DEBUG
    Serial.printf("%d pir=%d\r\n", now, pir);
#endif
  }
  int light = sampleLight();
  if (!on && pir && light > cfg.threshold) {
    on = true;
    power(on);
  }
  if (pir)
    last_activity = now;
  if (on && (now - last_activity) > 1000 * cfg.inactive_time) {
    on = false;
    power(on);
  }
  delay(1000 / HZ);
}
