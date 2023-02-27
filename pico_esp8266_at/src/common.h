#ifndef MAGB_COMMON_H
#define MAGB_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "libmobile/mobile.h"
#include "libmobile/inet_pton.h"

//Flash Config
#define FLASH_DATA_SIZE (FLASH_PAGE_SIZE * 3)
#define MOBILE_MAX_DATA_SIZE 0xFF

// Control the configs on Flash
bool haveAdapterConfig = false;
bool haveWifiConfig = false;
bool haveDNS1Config = false;
bool haveDNS2Config = false;
bool haveP2PConfig = false;
bool haveUNMETConfig = false;

//LED Config
#define LED_PIN       		  	25
#define LED_SET(A)    		  	(gpio_put(LED_PIN, (A)))
#define LED_ON        		  	LED_SET(true)
#define LED_OFF       		  	LED_SET(false)
#define LED_TOGGLE    		  	(gpio_put(LED_PIN, !gpio_get(LED_PIN)))

//Time Config
#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)

//UART RX Buffer Config
#define BUFF_AT_SIZE 2048 //2048 is the maximun you can receive from esp01
uint8_t buffATrx[BUFF_AT_SIZE+64] = {0}; // + extra bytes to hold the AT command answer echo
int buffATrx_pointer = 0;
uint8_t buffRecData[BUFF_AT_SIZE] = {0};
int buffRecData_pointer = 0;
int ipdVal[5] = {0,0,0,0,0};

//Wifi and Flash Configs Default
bool isConnectedWiFi = false;
char WiFiSSID[32] = "WiFi_Network";
char WiFiPASS[32] = "P@$$w0rd";

#define USE_CUSTOM_DNS1
char MAGB_DNS1[64] = "51.79.70.215";
#define USE_CUSTOM_DNS2
char MAGB_DNS2[64] = "192.168.0.126";
#define USE_CUSTOM_DNS_PORT
char MAGB_DNSPORT[5] = "53";

char P2P_PORT[5] = "1027";
char PKM_UNMETERED = '1';


struct esp_sock_config {
    int host_id;
    uint8_t host_type; //0=NONE, 1=TCP or 2=UDP
    enum mobile_addrtype host_iptype; //IPV4, IPV6 or NONE
    int local_port;
    bool sock_status;
};

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
    uint8_t config_eeprom[FLASH_DATA_SIZE];
    struct esp_sock_config esp_sockets[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
};
struct mobile_user *mobile;


// C Funciton to replace strcmp. Necessary to compare strings if the buffer have a 0x00 byte.
void *memmem(const void *l, size_t l_len, const void *s, size_t s_len){
	register char *cur, *last;
	const char *cl = (const char *)l;
	const char *cs = (const char *)s;

	/* we need something to compare */
	if (l_len == 0 || s_len == 0)
		return NULL;

	/* "s" must be smaller or equal to "l" */
	if (l_len < s_len)
		return NULL;

	/* special case where s_len == 1 */
	if (s_len == 1)
		return memchr(l, (int)*cs, l_len);

	/* the last position where its possible to find "s" in "l" */
	last = (char *)cl + l_len - s_len;

	for (cur = (char *)cl; cur <= last; cur++)
		if (cur[0] == cs[0] && memcmp(cur, cs, s_len) == 0)
			return cur;

	return NULL;
}

#endif