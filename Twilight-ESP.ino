#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <SimpleTimer.h>

#include "Configuration.h"

MDNSResponder mdns;
WiFiClient wifiClient;
PubSubClient mqtt_client(wifiClient);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
DNSServer dnsServer;
SimpleTimer timers;
config cfg;

#define PIR	D2
#define PIR_LED	D4
#define POWER	D1
#define HZ	0.5
#define SAMPLES	20

static enum State {
	START = -1,
	OFF = 0,
	ON = 1,
	AUTO_OFF = 2,
	AUTO_ON = 3,
	MQTT_OFF = 4,
	MQTT_ON = 5,
	DOMOTICZ_OFF = 6,
	DOMOTICZ_ON = 7
} state;

inline bool isOff() {
	return state == START || state == OFF || state == AUTO_OFF || state == MQTT_OFF || state == DOMOTICZ_OFF;
}

inline bool isOn() {
	return state == ON || state == AUTO_ON || state == MQTT_ON || state == DOMOTICZ_ON;
}

static bool connected, connecting;

static void flash(int pin, int ms, int n) {
	bool v = digitalRead(pin);
	for (int i = 0; i < n; i++) {
		digitalWrite(pin, !v);
		delay(ms);
		digitalWrite(pin, v);
		delay(ms);
	}
}

static volatile bool pir;

void IRAM_ATTR pir_handler() { pir = true; }

// timers
static unsigned light;
static unsigned fade;
static int watchdog, flasher, poller, connector, debugger;
static int fadeOn, fadeOff;

static void sampleLight() {
	static int samples[SAMPLES], pos;
	static long total;
	static int n;
	int sample = analogRead(A0);

	total += sample - samples[pos];
	samples[pos++] = sample;
	if (n < SAMPLES)
		n++;
	if (pos == SAMPLES) pos = 0;
	light = (int)(total / n);
}

static void activity() {
	timers.restartTimer(watchdog);
}

static void fade_off() {
	if (fade > cfg.off_bright)
		analogWrite(POWER, --fade);
}

static void fade_on() {
	if (fade < cfg.on_bright)
		analogWrite(POWER, ++fade);
}

const bool retain = true, dont_retain = false;

static void mqtt_pub(bool ret, const char *parent, const char *child, const char *fmt, ...);

static void debug() {
	uint32_t secs = millis() / 1000, mins = secs / 60, hours = mins / 60, days = hours / 24;
	mqtt_pub(retain, cfg.stat_topic, PSTR("uptime"), PSTR("%d %d days %02d:%02d"), secs, days, (hours % 24), (mins % 60));
	mqtt_pub(retain, cfg.stat_topic, PSTR("mem"), PSTR("%d/%d/%d"), ESP.getFreeHeap(), ESP.getHeapFragmentation(), ESP.getMaxFreeBlockSize());
	mqtt_pub(retain, cfg.stat_topic, PSTR("fade"), PSTR("%d %d %d"), fade, cfg.off_bright, cfg.on_bright);
	mqtt_pub(dont_retain, cfg.stat_topic, PSTR("rssi"), PSTR("%d"), (int)WiFi.RSSI());
}

static void captive_portal() {
	WiFi.mode(WIFI_AP);
	WiFi.softAP(cfg.hostname);
	Serial.print(F("Connect to SSID: "));
	Serial.print(cfg.hostname);
	Serial.println(F(" to configure WIFI"));
	dnsServer.start(53, "*", WiFi.softAPIP());
}

static void flash_connecting() {
	static int i;
	const char s[] = "|/-\\";

	connected = WiFi.status() == WL_CONNECTED;
	if (i < 240 && !connected) {
		digitalWrite(PIR_LED, !digitalRead(PIR_LED));
		Serial.print(s[i % 4]);
		Serial.print('\r');
		i++;
		return;
	}
	Serial.println();
	connecting = false;

	if (connected) {
		Serial.print(F("Connected to "));
		Serial.println(cfg.ssid);
		Serial.println(WiFi.localIP());

		if (mdns.begin(cfg.hostname, WiFi.localIP())) {
			Serial.println(F("mDNS started"));
			mdns.addService("http", "tcp", 80);
		} else
			Serial.println(F("Error starting MDNS"));

		mqtt_pub(retain, cfg.stat_topic, PSTR("restart"), ESP.getResetInfo().c_str());
		mqtt_pub(retain, cfg.stat_topic, PSTR("built"), PSTR(BUILD_DATE));
		mqtt_pub(retain, cfg.stat_topic, PSTR("core"), PSTR("%d"), esp8266::coreVersionNumeric());
		mqtt_pub(retain, cfg.stat_topic, PSTR("wifi"), PSTR("%s %d %s"), WiFi.macAddress().c_str(), WiFi.channel(), WiFi.localIP().toString().c_str());
		if (cfg.debug)
			debugger = timers.setInterval(60000, debug);

		flash(POWER, 250, 2);
		return;
	}

	captive_portal();
}

static bool mqtt_connect(PubSubClient &c) {
	if (!connected)
		return false;
	if (c.connected())
		return true;
	if (c.connect(cfg.hostname)) {
		c.subscribe(cfg.cmnd_topic);
		if (cfg.switch_idx != -1)
			c.subscribe(cfg.from_domoticz);
		return true;
	}
	Serial.print(F("MQTT connection to: "));
	Serial.print(cfg.mqtt_server);
	Serial.print(F(" failed, rc="));
	Serial.println(mqtt_client.state());
	flash(PIR_LED, 100, 2);
	return false;
}

static void domoticz_pub(int idx, int val) {
	if (idx != -1 && *cfg.to_domoticz && mqtt_connect(mqtt_client)) {
		char msg[64];
		snprintf_P(msg, sizeof(msg), PSTR("{\"idx\":%d,\"nvalue\":%d,\"svalue\":\"\"}"), idx, val);
		mqtt_client.publish(cfg.to_domoticz, msg, retain);
	}
}

static void mqtt_pub(bool ret, const char *parent, const char *child, const char *fmt, ...) {
	if (mqtt_connect(mqtt_client)) {
		char msg[64], topic[TOPIC_LEN];
		snprintf_P(topic, sizeof(topic), PSTR("%s/%s"), parent, child);
		va_list args;
		va_start(args, fmt);
		vsnprintf_P(msg, sizeof(msg), fmt, args);
		mqtt_client.publish(topic, msg, ret);
		va_end(args);
	}
}

static void pir_event(int p) {
	mqtt_pub(dont_retain, cfg.stat_topic, PSTR("pir"), PSTR("%d"), p);
	domoticz_pub(cfg.pir_idx, p);
}

static void mqtt_callback(char *topic, byte *payload, unsigned int length) {
	if (strncasecmp(topic, cfg.cmnd_topic, strlen(cfg.cmnd_topic)) == 0) {
		activity();
		bool cmdOn = (*payload == '1');
		if (cmdOn && isOff())
			state = MQTT_ON;
		else if (!cmdOn && isOn())
			state = MQTT_OFF;
	} else if (cfg.switch_idx > 0 && length >= 20 && strncasecmp(topic, cfg.from_domoticz, strlen(cfg.from_domoticz)) == 0) {
		DynamicJsonDocument doc(JSON_OBJECT_SIZE(16) + 500);
		auto error = deserializeJson(doc, payload);
		if (error)
			return;
		int idx = doc[F("idx")] | -1, v = doc[F("nvalue")] | -1;
		if (idx == cfg.switch_idx && v >= 0) {
			activity();
			if (v == 1 && isOff())
				state = DOMOTICZ_ON;
			else if (v == 0 && isOn())
				state = DOMOTICZ_OFF;
		}
	}
}

void setup() {
	Serial.begin(115200);
	Serial.println(F("Booting!"));

	pinMode(PIR, INPUT);
	pinMode(POWER, OUTPUT);
	pinMode(PIR_LED, OUTPUT);

	flash(PIR_LED, 500, 2);
	flash(POWER, 500, 2);

	bool result = LittleFS.begin();
	if (!result) {
		Serial.print(F("LittleFS: "));
		Serial.println(result);
		return;
	}

	if (!cfg.read_file("/config.json")) {
		Serial.println(F("config!"));
		return;
	}

	Serial.println(F("--networking--"));
	Serial.print(F("MAC: "));
	Serial.println(WiFi.macAddress());
	Serial.print(F("SSID: "));
	Serial.println(cfg.ssid);
	Serial.print(F("Password: "));
	Serial.println(cfg.password);
	Serial.print(F("Hostname: "));
	Serial.println(cfg.hostname);
	Serial.println(F("----light-----"));
	Serial.print(F("Inactive time: "));
	Serial.println(cfg.inactive_time);
	Serial.print(F("Threshold: "));
	Serial.println(cfg.threshold);
	Serial.print(F("On delay: "));
	Serial.println(cfg.on_delay);
	Serial.print(F("Off delay: "));
	Serial.println(cfg.off_delay);
	Serial.print(F("On bright: "));
	Serial.println(cfg.on_bright);
	Serial.print(F("Off bright: "));
	Serial.println(cfg.off_bright);
	Serial.println(F("-----mqtt-----"));
	Serial.print(F("MQTT Server: "));
	Serial.println(cfg.mqtt_server);
	Serial.print(F("Notification time: "));
	Serial.println(cfg.interval_time);
	Serial.print(F("Debugging: "));
	Serial.println(cfg.debug);
	Serial.println(F("---domoticz---"));
	Serial.print(F("Switch idx: "));
	Serial.println(cfg.switch_idx);
	Serial.print(F("PIR idx: "));
	Serial.println(cfg.pir_idx);
	Serial.print(F("To Domoticz: "));
	Serial.println(cfg.to_domoticz);
	Serial.print(F("From Domoticz: "));
	Serial.println(cfg.from_domoticz);

	WiFi.mode(WIFI_STA);
	WiFi.setSleepMode(WIFI_NONE_SLEEP);
	WiFi.hostname(cfg.hostname);
	if (*cfg.ssid) {
		WiFi.setAutoReconnect(true);
		WiFi.begin(cfg.ssid, cfg.password);
		connecting = true;
	} else
		captive_portal();

	server.on("/config", HTTP_POST, []() {
		if (server.hasArg("plain")) {
			String body = server.arg("plain");
			File f = LittleFS.open("/config.json", "w");
			f.print(body);
			f.close();
			server.send(200);
			ESP.restart();
		} else
			server.send(400, "text/plain", "No body!");
	});
	server.serveStatic("/", LittleFS, "/index.html");
	server.serveStatic("/config", LittleFS, "/config.json");
	server.serveStatic("/js/transparency.min.js", LittleFS, "/transparency.min.js");
	server.serveStatic("/info.png", LittleFS, "/info.png");

	httpUpdater.setup(&server);
	server.begin();

	mqtt_client.setServer(cfg.mqtt_server, 1883);
	mqtt_client.setCallback(mqtt_callback);

	digitalWrite(POWER, LOW);
	digitalWrite(PIR_LED, HIGH);
	attachInterrupt(PIR, pir_handler, RISING);

	fadeOff = timers.setInterval(cfg.off_delay, fade_off);
	timers.disable(fadeOff);

	fadeOn = timers.setInterval(cfg.on_delay, fade_on);
	timers.disable(fadeOn);

	watchdog = timers.setInterval(cfg.inactive_time, []() { state = AUTO_OFF; });
	timers.disable(watchdog);

	timers.setInterval(cfg.interval_time, []() {
		mqtt_pub(dont_retain, cfg.stat_topic, PSTR("light"), PSTR("%d"), light);
	});
	timers.setInterval(1000 / HZ, sampleLight);
	sampleLight();

	connector = timers.setInterval(250, flash_connecting);
	flasher = timers.setInterval(1000, []() { digitalWrite(PIR_LED, !digitalRead(PIR_LED)); });
	poller = timers.setInterval(100, []() { mqtt_client.loop(); });
}

void loop() {
	mdns.update();
	server.handleClient();

	if (connected) {
		digitalWrite(PIR_LED, !pir);
		timers.disable(flasher);
		timers.disable(connector);
		timers.enable(poller);
	} else if (connecting)
		timers.enable(connector);
	else {
		dnsServer.processNextRequest();
		timers.enable(flasher);
		timers.disable(poller);
		timers.disable(connector);
	}

	if (pir) {
		activity();
		pir = false;
		if (light > cfg.threshold && isOff())
			state = AUTO_ON;
		else
			pir_event(1);
	}
	switch (state) {
	case START:
		state = OFF;
		fade = cfg.off_bright;
		break;
	case OFF:
		analogWrite(POWER, cfg.off_bright);
		break;
	case AUTO_OFF:
	case MQTT_OFF:
	case DOMOTICZ_OFF:
		timers.disable(fadeOn);
		if (fade > cfg.off_bright)
			timers.enable(fadeOff);
		else {
			timers.disable(fadeOff);
			timers.disable(watchdog);
			domoticz_pub(cfg.switch_idx, false);
			state = OFF;
		}
		break;
	case ON:
		analogWrite(POWER, cfg.on_bright);
		break;
	case AUTO_ON:
	case MQTT_ON:
	case DOMOTICZ_ON:
		timers.disable(fadeOff);
		if (fade < cfg.on_bright)
			timers.enable(fadeOn);
		else {
			timers.disable(fadeOn);
			timers.enable(watchdog);
			timers.restartTimer(watchdog);
			domoticz_pub(cfg.switch_idx, true);
			state = ON;
		}
		break;
	}

	static State last_state = START;
	if (state != last_state) {
		if (last_state == AUTO_ON)
			pir_event(1);

		last_state = state;
		mqtt_pub(retain, cfg.stat_topic, PSTR("state"), PSTR("%d"), state);
	}
	timers.run();
}
