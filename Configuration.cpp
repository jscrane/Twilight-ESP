#include <FS.h>
#include <ArduinoJson.h>
#include "Configuration.h"

bool Configuration::read_file(const char *filename) {
	File f = SPIFFS.open(filename, "r");
	if (!f)
		return false;

	DynamicJsonDocument doc(JSON_OBJECT_SIZE(13) + 300);
	auto error = deserializeJson(doc, f);
	f.close();
	if (error)
		return false;

	configure(doc);
	return true;
}
