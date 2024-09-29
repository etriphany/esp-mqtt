
<h1 align="center">ESP MQTT</h1>
<p align="center">
    <img src="https://raw.githubusercontent.com/etriphany/esp-mqtt/master/docs/img/esp01.jpeg" height="225"/>
    <img src="https://raw.githubusercontent.com/etriphany/esp-mqtt/master/docs/img/mqtt.png" height="225"/>
</p>

# Description
Compact and reliable [MQTT](http://mqtt.org/) client for IoT projects based on __ESP8266__

* Full based on [Espressif NonOS](https://www.espressif.com/en/support/download/sdks-demos)
* Self contained, no external libraries

# Features
Major MQTT V4 featues:
  * Authentication
  * Clean session flag
  * Client identifier
  * Keep alive
  * Last will and testament
  * TLS encryption
  * QoS levels
    - At most once `QoS 0`
  * Topic name matching
    - Multi-level wildcard `#`
    - Single-level wildcart `+`
    - Exact match

Additional features:
  * Message subscription router
    * Single handler per client
    * Single handler per subscription
    * Fallback handler
  * MQTT packet printer

# Build Example
The project includes a Makefile that can be used to flash into an ESP8266
Select the target flash address using PARAM_APP

- 0 = Non FOTA (single firmware)
- 1 = Fota app address 1
- 2 = Fota app address 2

```sh
  cd src
  make PARAM_APP=0 && make image PARAM_APP=0 && make flash PARAM_APP=0
```