#include <FS.h>
#include <ArduinoJson.h>
#include "Configuration.h"

bool Configuration::read_file(const char *filename) {
	File f = SPIFFS.open(filename, "r");
	if (!f) {
		Serial.println("failed to open config file");
		return false;
	}

	DynamicJsonDocument doc(JSON_OBJECT_SIZE(18) + 600);
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
