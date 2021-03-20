# Shelly-Dimmer-2
Custome firmware for the Shelly Dimmer 2

This is a firmware for the Shelly Dimmer 2 (see https://shelly.cloud/knowledge-base/devices/shelly-dimmer-2/). This firmware has been developped with the following libraries:
- ESP8266TimerInterrupt 1.2.0: https://github.com/khoih-prog/ESP8266TimerInterrupt
- WifiManager 2.0.3-alpha: https://github.com/tzapu/WiFiManager/tree/development
- Adafruit MQTT Library 2.1.0: https://github.com/adafruit/Adafruit_MQTT_Library
- Arduino IDE 1.8.3

This firmware can be installed by connecting the Shelly device to a PC with an USB-to-UART adapter and flashing the firmware with the esptools. The firmware can be also flashed through the OTA (Over The Air) programming. This is done by first installing Tasmota on the device using the mgos-to-tasmota software (https://github.com/yaourdt/mgos-to-tasmota). Once Tasmota has been installed to the SHelly device, the firmware can be uploaded using the following gzip file https://github.com/Mollayo/Shelly-Dimmer-2/raw/master/shellyDimmer2.ino.generic.bin.gz.

Before installing this firmware, the Shelly stock firmware (<a href="https://github.com/Mollayo/Shelly-Dimmer-2-Reverse-Engineering/blob/master/shelly%20stock%20firmware/shelly_dimmer_2%2020200904-094614%20v1.8.4%40699b08ac.bin">20200904-094614/v1.8.4@699b08ac</a>) has to be installed on the device and the device should run once so that the correct version of the STM32 firmware is installed. It is also possible to change the STM32 firmware directly from the configuration webpage of the device.

