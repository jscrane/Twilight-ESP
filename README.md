View this project on [CADLAB.io](https://cadlab.io/project/1212).

# Twilight
Twilight for ESP. A new spin on an old idea.

See [blog articles](https://programmablehardware.blogspot.ie/search?q=twilight).

## Requires:
- Arduino 1.8.9
- ESP8266 [Arduino core](https://github.com/esp8266/Arduino) 2.4.2
- Wemos [D1 Mini](https://wiki.wemos.cc/products:d1:d1_mini)
- [PubSubClient](https://pubsubclient.knolleary.net) 2.7
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) 6.11.0

(Note the use of an older ESP8266 core. This is because this sketch can't
reliably connect to WiFi with 2.5.2.)

## Features:
- Web [configuration](https://github.com/jscrane/WebConfiguredESP)
- [Domoticz](https://domoticz.com) integration via MQTT

## Circuit Diagram
![Schematic](eagle/schematic.png)

