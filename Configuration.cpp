#include <FS.h>
#include <ArduinoJson.h>
#include "Configuration.h"

bool Configuration::read_file(const char *filename) {
  File f = SPIFFS.open(filename, "r");
  if (!f)
    return false;

  char buf[512];
  f.readBytes(buf, sizeof(buf));
  DynamicJsonBuffer json(JSON_OBJECT_SIZE(11) + 210);
  JsonObject &root = json.parseObject(buf);
  configure(root);
  f.close();
  return true;
}

void Configuration::strncpy_null(char *dest, const char *src, int n) {
  if (src)
    strncpy(dest, src, n);
  else
    *dest = 0;
}

