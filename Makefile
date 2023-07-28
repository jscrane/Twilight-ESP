BOARD := d1_mini
BAUD := 921600
TERMINAL_SPEED := 115200
FLASH_SIZE := 4M1M
F_CPU := 80
LWIP_OPTS := hb2f
CPPFLAGS := -DBUILD_DATE=\""${shell date}\"" -DMQTT_VERSION=3 -DMQTT_KEEPALIVE=3600 -DMQTT_MAX_PACKET_SIZE=1000
PREBUILD := data/config.json

include esp8266.mk

data/config.json: config.skel
	cp $^ $@
