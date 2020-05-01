#ifndef __CONFIGURATION_H__
#define __CONFIGURATION_H__

class Configuration {
public:
	bool read_file(const char *filename);

protected:
	virtual void configure(class JsonDocument &doc) = 0;
};

#define NETWORK_LEN	33
#define TOPIC_LEN	65

class config: public Configuration {
public:
	char ssid[NETWORK_LEN];
	char password[NETWORK_LEN];
	char hostname[NETWORK_LEN];
	char mqtt_server[NETWORK_LEN];
	long interval_time;
	long inactive_time;
	unsigned threshold;
	int switch_idx;
	int pir_idx;
	unsigned on_delay;
	unsigned off_delay;
	unsigned on_bright;
	unsigned off_bright;
	char stat_topic[TOPIC_LEN];
	char cmnd_topic[TOPIC_LEN];
	char to_domoticz[TOPIC_LEN];
	char from_domoticz[TOPIC_LEN];
	bool domoticz_sub, debug;

	void configure(JsonDocument &o);
};

#endif
