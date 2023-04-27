#include "flash_eeprom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/time.h"

#include <mobile.h>

bool needWrite = false;

const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

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

//512 bytes for the Mobile Adapter GB + Adapter Configs and 256 bytes to WiFi Config and other stuffs
void FormatFlashConfig(){
    printf("Erasing target region... ");
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_DATA_SIZE);
    printf("Done.\n");
}

bool ReadConfigOption(uint8_t * buff, int offset, char *key, int datasize, char *varConfig){
    if(memmem(buff+offset,strlen(key),key,strlen(key)) != NULL){
        memset(varConfig,0x00,sizeof(varConfig));
        memcpy(varConfig,buff+(offset+strlen(key)),datasize-strlen(key));        
    }else{
        char tmp_config[datasize];
        memset(tmp_config,0x00,sizeof(tmp_config));
        sprintf(tmp_config,"%s%s",key,varConfig);
        memcpy(buff+offset,tmp_config,sizeof(tmp_config));
        needWrite = true;
    }
    return true;
}

//Read flash memory and set the configs
bool ReadFlashConfig(uint8_t * buff, char * WiFiSSID, char * WiFiPASS){
    printf("Reading the target region... ");
    memset(buff,0x00,FLASH_DATA_SIZE);
    memcpy(buff,flash_target_contents,FLASH_DATA_SIZE);
    
    //Check if the Flash is already formated 
    if(memmem(buff+OFFSET_CONFIG,strlen(KEY_CONFIG),KEY_CONFIG,strlen(KEY_CONFIG)) == NULL){
        char tmp_config[16];
        memset(tmp_config,0x00,sizeof(tmp_config));
        sprintf(tmp_config,"%s",KEY_CONFIG);
        memcpy(buff+OFFSET_CONFIG,tmp_config,sizeof(tmp_config));
    }
    //Read the WiFi Config (32 bytes each with KEY)
    if(memmem(buff+OFFSET_SSID,strlen(KEY_SSID),KEY_SSID,strlen(KEY_SSID)) != NULL && memmem(buff+OFFSET_PASS,strlen(KEY_PASS),KEY_PASS,strlen(KEY_PASS)) != NULL){
        char tmp_ssid[28];
        memset(tmp_ssid,0x00,sizeof(tmp_ssid));
        memcpy(tmp_ssid,buff+(OFFSET_SSID+strlen(KEY_SSID)),32-strlen(KEY_SSID));
        strcpy(WiFiSSID,tmp_ssid);
        
        char tmp_pass[28];
        memset(tmp_pass,0x00,sizeof(tmp_pass));
        memcpy(tmp_pass,buff+(OFFSET_PASS+strlen(KEY_PASS)),32-strlen(KEY_PASS));
        strcpy(WiFiPASS,tmp_pass);
    }else{
        char tmp_ssid[32];
        memset(tmp_ssid,0x00,sizeof(tmp_ssid));
        sprintf(tmp_ssid,"%s%s",KEY_SSID,WiFiSSID);
        memcpy(buff+OFFSET_SSID,tmp_ssid,sizeof(tmp_ssid));
        char tmp_pass[32];
        memset(tmp_pass,0x00,sizeof(tmp_pass));
        sprintf(tmp_pass,"%s%s",KEY_PASS,WiFiPASS);
        memcpy(buff+OFFSET_PASS,tmp_pass,sizeof(tmp_pass));
    }

    printf("Done.\n");
    return true;
}

void SaveFlashConfig(uint8_t * buff){
    FormatFlashConfig();
    printf("Programming target region... ");
    flash_range_program(FLASH_TARGET_OFFSET, buff, FLASH_DATA_SIZE);
    printf("Done.\n");
}

void RefreshConfigBuff(uint8_t * buff, char * WiFiSSID, char * WiFiPASS){    
    char tmp_ssid[32];
    memset(tmp_ssid,0x00,sizeof(tmp_ssid));
    sprintf(tmp_ssid,"%s%s",KEY_SSID,WiFiSSID);
    memcpy(buff+OFFSET_SSID,tmp_ssid,sizeof(tmp_ssid));
    char tmp_pass[32];
    memset(tmp_pass,0x00,sizeof(tmp_pass));
    sprintf(tmp_pass,"%s%s",KEY_PASS,WiFiPASS);
    memcpy(buff+OFFSET_PASS,tmp_pass,sizeof(tmp_pass));

    FormatFlashConfig();
    printf("Programming target region... ");
    flash_range_program(FLASH_TARGET_OFFSET, buff, FLASH_DATA_SIZE);
    printf("Done.\n");
}