#include <FS.h>
#include "Configuration.h"

bool Configuration::read_file(const char *filename) {
  File f = SPIFFS.open(filename, "r");
  if (!f)
    return false;

  char buf[512];
  f.readBytes(buf, sizeof(buf));
  char *b = buf;
  for (;;) {
    const char *p = strsep(&b, "=\n");
    if (!p)
      break;
    if (*p != '#') {
      const char *q = strsep(&b, "\n");
      configure(p, q);
    }
  }
  f.close();
  return true;
}

