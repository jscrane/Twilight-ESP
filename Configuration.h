#ifndef __CONFIGURATION_H__
#define __CONFIGURATION_H__

class Configuration {
public:
  bool read_file(const char *filename);

  virtual bool value(const char *key, char *value, int n) { return false; }
  virtual void entry(const char *key, const char *value) = 0;
};

#endif
