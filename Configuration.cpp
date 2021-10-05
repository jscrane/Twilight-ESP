#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Configuration.h"

bool Configuration::read_file(const char *filename) {
	File f = LittleFS.open(filename, "r");
	if (!f) {
		Serial.println("failed to open config file");
		return false;
	}

	DynamicJsonDocument doc(JSON_OBJECT_SIZE(19) + 600);
	auto error = deserializeJson(doc, f);
	f.close();
	if (error) {
		Serial.print("json read failed: ");
		Serial.println(error.c_str());
		return false;
	}

	configure(doc);
	return true;
}

void config::configure(JsonDocument &o) {
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
	on_bright = o[F("on_bright")] | 1023;
	off_bright = o[F("off_bright")] | 0;
	strlcpy(stat_topic, o[F("stat_topic")] | "", sizeof(stat_topic));
	strlcpy(cmnd_topic, o[F("cmnd_topic")] | "", sizeof(cmnd_topic));
	strlcpy(to_domoticz, o[F("to_domoticz")] | "", sizeof(to_domoticz));
	strlcpy(from_domoticz, o[F("from_domoticz")] | "", sizeof(from_domoticz));
	debug = o[F("debug")];
}
