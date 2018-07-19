#include "espressif/esp_common.h"
#include "esp/uart.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "ssid_config.h"

#include "multipwm.h"
#include "E131.h"

e131_packet_t pbuff; /* Packet buffer */
e131_packet_t *pwbuff; /* Pointer to working packet buffer */

//DMX Channel parameter
uint16_t dmxChannelStart = 1;
uint8_t dmxDefaultLightLevel = 10;
uint8_t dmxDefaultGlitchTiming = 150; //4 * 150 = 600 seconds

//Set up PWM for pins
uint8_t pins[] = {2}; //NodeMCU D4, D2 https://github.com/nodemcu/nodemcu-devkit-v1.0#pin-map
uint8_t pinDutyOut[] = {0};

//Glitch state machine parameters
uint32_t glitchNextGlitch[] = {(uint32_t)4000};
uint8_t glitchGlitchEnabled[] = {2};
uint32_t glitchGlitchRandomMin[] = {(uint16_t)0};
uint32_t glitchGlitchRandomMax[] = {(uint16_t)0};

typedef enum {
	DMX_LEVEL = 0,
	DMX_GLITCH = 1
} dmx_sub_adress_t;

uint16_t getDMXChannel(uint8_t id, dmx_sub_adress_t sub) {
	return dmxChannelStart + (id*2) + sub;
}

void e131task(void *pvParameters) {
	printf("Open server.\r\n");
	vTaskDelay(1000);

	struct netconn *conn;
	err_t err;

	/* Create a new connection handle */
	conn = netconn_new(NETCONN_UDP);
	if(!conn) {
		printf("Error: Failed to allocate socket.\r\n");
		return;
	}

	/* Bind to port with default IP address */
	err = netconn_bind(conn, IP_ADDR_ANY, E131_DEFAULT_PORT);
	if(err != ERR_OK) {
		printf("Error: Failed to bind socket. err=%d\r\n", err);
		return;
	}

	ip4_addr_t multiaddr;
	IP4_ADDR(&multiaddr, 239, 255, 0, 1); //IPv4 local scope multicast

	err = netconn_join_leave_group(conn, &multiaddr, &netif_default->ip_addr, NETCONN_JOIN);
	if(err != ERR_OK) {
		printf("Error: Join Multicast Group. err=%d\r\n", err);
		return;
	}

	printf("Listening for connections.\r\n");

	while(1) {
		struct netbuf *buf;

		err = netconn_recv(conn, &buf);
		if(err != ERR_OK) {
			printf("Error: Failed to receive packet. err=%d\r\n", err);
			continue;
		}

		if(buf->p->tot_len == sizeof(pwbuff->raw)) {
			//If packet is 638 bytes we handle it as a correct package and copy it to a global struct
			if(netbuf_copy(buf, pwbuff->raw, sizeof(pwbuff->raw)) != buf->p->tot_len) {
				printf("Error: Couldn't copy buffer. err=%d\r\n", err);
			}
		} else {
			printf("Wrong packet size.\n\n");
		}

		netbuf_delete(buf);
	}
}

void pwmtask(void *pvParameters) {
	//Set up PWM for pins
	pwm_info_t pwm_info;
	pwm_info.channels = sizeof(pins);
	pwm_info.reverse = false;
	multipwm_init(&pwm_info);
	for (uint8_t ii=0; ii<pwm_info.channels; ii++) {
		multipwm_set_pin(&pwm_info, ii, pins[ii]);
	}

    while(1) {
		multipwm_stop(&pwm_info);

		uint16_t channel[pwm_info.channels];
		for (uint8_t i=0; i<pwm_info.channels; i++) {
			channel[i] = pinDutyOut[i]; //Current duty cycle to be set on channel
			//Upscale 8 bit DMX value to 16 bit, and add original value to fit the range from 0-65535
			channel[i] = (channel[i] << 8) + channel[i];
			multipwm_set_duty(&pwm_info, i, channel[i]);
		}

		multipwm_start(&pwm_info);
		vTaskDelay(1);
	}
}

uint16_t randomRange(uint16_t minimum_number, uint16_t max_number) {
	return rand() % (max_number + 1 - minimum_number) + minimum_number;
}

void calculateMinMaxTiming(uint8_t id) {
	//Get DMX glitch channel value
	glitchGlitchEnabled[id] = pwbuff->property_values[getDMXChannel(id, DMX_GLITCH)];
	uint32_t msTime = glitchGlitchEnabled[id];
	//Convert it to milliseconds and multiply to get a arbitrary higher value
	msTime = msTime * 1000 * 4;
	uint16_t percentage = (uint16_t)(0.2 * msTime);
	//If glitch is set to zero, disable glitching, else set min max time
	if(glitchGlitchEnabled[id] != 0) {
		glitchGlitchRandomMin[id] = msTime - percentage;
		glitchGlitchRandomMax[id] = msTime + percentage;
	}
}

void calculateNextGlitch(uint8_t id) {
	uint32_t time = sys_now();
	bool makeShortGlitch = false;
	//If level is off, it should generate a short glitch to light up soon
	if (pinDutyOut[id] == 0) {
		makeShortGlitch = true;
	} else {
		//If on, randimize if to make a short or a long glitch
		if(randomRange(0, 2) == 0) {
			makeShortGlitch = false;
		} else {
			makeShortGlitch = true;
		}
	}
	calculateMinMaxTiming(id);
	uint16_t randomDelay = 0;
	if (makeShortGlitch) {
		randomDelay = randomRange(60, 300);
	} else {
		randomDelay = randomRange(glitchGlitchRandomMin[id], glitchGlitchRandomMax[id]);
	}
	glitchNextGlitch[id] = time + randomDelay;
	//Only print if glitch is enabled
	if (glitchGlitchEnabled[id] != 0) {
		printf("Next delay in: %dms\n", randomDelay);
	}
}

void glitch(uint8_t id) {
	//Toggle level for a glitch effect
	if(pinDutyOut[id] == 0) {
		pinDutyOut[id] = pwbuff->property_values[getDMXChannel(id, DMX_LEVEL)]; //Get DMX channel value from sACN struct
	} else {
		//Only turn off if glitch is enabled
		if (glitchGlitchEnabled[id] != 0) {
			pinDutyOut[id] = 0;
		}
	}
}

void checkLevelAgainstDMX(uint8_t id) {
	//Only update level if it's not in an off-state
	if(pinDutyOut[id] != 0) {
		//Get DMX channel value from sACN struct
		pinDutyOut[id] = pwbuff->property_values[getDMXChannel(id, DMX_LEVEL)];
	}
	//If glitch is updated, set it
	if (glitchGlitchEnabled[id] != pwbuff->property_values[getDMXChannel(id, DMX_GLITCH)]) {
		glitchGlitchEnabled[id] = pwbuff->property_values[getDMXChannel(id, DMX_GLITCH)];
		printf("New glitch %d\n", glitchGlitchEnabled[id]);
		calculateNextGlitch(id);
	}
}

void glitchtask(void *pvParameters) {
	//Set up glitching
	for (uint8_t i = 0; i < sizeof(pins); i++) {
		pinDutyOut[i] = 0;
	}

	while(1) {
		uint32_t time = sys_now();
		for (uint8_t i = 0; i < sizeof(pins); i++) {
			if (time >= glitchNextGlitch[i]) {
				glitch(i);
				calculateNextGlitch(i);
			}
			checkLevelAgainstDMX(i);
		}
		vTaskDelay(1);
	}
}

void user_init(void) {
    uart_set_baud(0, 115200);
    printf("SDK version:%s\n", sdk_system_get_sdk_version());

    struct sdk_station_config config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    };
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);

	memset(pbuff.raw, 0, sizeof(pbuff.raw));
	pwbuff = &pbuff;
	for (uint8_t id = 0; id < sizeof(pins); id++) {
		//Set default light level
		pwbuff->property_values[getDMXChannel(id, DMX_LEVEL)] = dmxDefaultLightLevel;
		pinDutyOut[id] = 0;

		//Set default glitch timings
		glitchGlitchEnabled[id] = dmxDefaultGlitchTiming;
		pwbuff->property_values[getDMXChannel(id, DMX_GLITCH)] = dmxDefaultGlitchTiming;
	}
	xTaskCreate(e131task, "e131task", 768, NULL, 8, NULL);
	xTaskCreate(pwmtask, "pwmtask", 256, NULL, 2, NULL);
	xTaskCreate(glitchtask, "glitchtask", 256, NULL, 2, NULL);
}

