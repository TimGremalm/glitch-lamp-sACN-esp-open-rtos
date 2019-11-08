# glitch-lamp-sACN-esp-open-rtos
Glitching lamp effect using 2 channels over sACN (E1.31, DMX512) firmware for ESP8266 running esp-open-rtos

# sACN (E1.31)
* Listening on multicast ```239.255.0.1``` port ```5568```
* Programmed channel start will be the first DMX Channel
* DMX channel 1: Dim level (Default 10)
	* 0:		Lamp Off
	* 1-255:	Low to Full Dim Level
* DMX channel 2: Glitchiness (Default is 150 (150*4=600s))
	* 0:		No Glitch
	* 1-255:	Units*4 = Seconds until next glitch

# Setup WiFI
Edit ```private_ssid_config.h under``` ```$ESPOPENRTOS/include ```

# Setup first DMX Channel
Change ```uint16_t dmxChannelStart = 1;```

# Electrical
* PWM out on GPIO2 (Pin D4 on NodeMCU, https://github.com/nodemcu/nodemcu-devkit-v1.0#pin-map)

