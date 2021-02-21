# Shelly-Dimmer-2
Custome firmware for the Shelly Dimmer 2

This is a firmware for the Shelly Dimmer 2 (see https://shelly.cloud/knowledge-base/devices/shelly-dimmer-2/). This firmware has been developped with the following libraries:
- ESP8266TimerInterrupt 1.0.3: https://github.com/khoih-prog/ESP8266TimerInterrupt
- WifiManager 2.0.3-alpha: https://github.com/tzapu/WiFiManager/tree/development
- Adafruit MQTT Library 1.3.0: https://github.com/adafruit/Adafruit_MQTT_Library
- Arduino IDE 1.8.3

Before installing this firmware, the Shelly stock firmware (<a href="https://github.com/Mollayo/Shelly-Dimmer-2-Reverse-Engineering/blob/master/shelly%20stock%20firmware/shelly_dimmer_2%2020200904-094614%20v1.8.4%40699b08ac.bin">20200904-094614/v1.8.4@699b08ac</a>) has to be installed on the device and the device should run once so that the correct version of the STM32 firmware is installed.
