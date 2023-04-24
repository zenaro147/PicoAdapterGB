#ifndef FLASH_EEPROM_H
#define FLASH_EEPROM_H

#include "common.h"

//Flash reprograming example https://github.com/raspberrypi/pico-examples/blob/master/flash/program/flash_program.c
//https://www.makermatrix.com/blog/read-and-write-data-with-the-pi-pico-onboard-flash/


#define KEY_CONFIG "CONFIG"
#define KEY_SSID "SSID"
#define KEY_PASS "PASS"


#define OFFSET_CONFIG 0
#define OFFSET_MAGB 16
#define OFFSET_SSID MOBILE_CONFIG_SIZE+OFFSET_MAGB
#define OFFSET_PASS OFFSET_SSID+32

bool needWrite = false;

#define FLASH_TARGET_OFFSET (FLASH_DATA_SIZE * 1024)
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

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
bool ReadFlashConfig(uint8_t * buff){
    printf("Reading the target region... ");
    memcpy(buff,flash_target_contents,FLASH_DATA_SIZE);
    //Check if the Flash is already formated 
    if(memmem(buff+OFFSET_CONFIG,strlen(KEY_CONFIG),KEY_CONFIG,strlen(KEY_CONFIG)) == NULL){
        char tmp_config[16];
        memset(tmp_config,0x00,sizeof(tmp_config));
        sprintf(tmp_config,"%s",KEY_CONFIG);
        memcpy(buff+OFFSET_CONFIG,tmp_config,sizeof(tmp_config));
        needWrite = true;
    }
    
    //Read the WiFi Config (32 bytes each with KEY)
    if(memmem(buff+OFFSET_SSID,strlen(KEY_SSID),KEY_SSID,strlen(KEY_SSID)) != NULL && memmem(buff+OFFSET_PASS,strlen(KEY_PASS),KEY_PASS,strlen(KEY_PASS)) != NULL){
        memset(WiFiSSID,0x00,sizeof(WiFiSSID));
        memcpy(WiFiSSID,buff+(OFFSET_SSID+strlen(KEY_SSID)),32-strlen(KEY_SSID));
        
        memset(WiFiPASS,0x00,sizeof(WiFiPASS));
        memcpy(WiFiPASS,buff+(OFFSET_PASS+strlen(KEY_PASS)),32-strlen(KEY_PASS));
    }else{
        char tmp_ssid[32];
        memset(tmp_ssid,0x00,sizeof(tmp_ssid));
        sprintf(tmp_ssid,"%s%s",KEY_SSID,WiFiSSID);
        memcpy(buff+OFFSET_SSID,tmp_ssid,sizeof(tmp_ssid));
        char tmp_pass[32];
        memset(tmp_pass,0x00,sizeof(tmp_pass));
        sprintf(tmp_pass,"%s%s",KEY_PASS,WiFiPASS);
        memcpy(buff+OFFSET_PASS,tmp_pass,sizeof(tmp_pass));
        needWrite = true;
    }
    haveWifiConfig = true;

    if(needWrite){
        FormatFlashConfig();
        flash_range_program(FLASH_TARGET_OFFSET, buff, FLASH_DATA_SIZE);
        needWrite = false;
    }

    printf("Done.\n");
    return true;
}

void SaveFlashConfig(uint8_t * buff){
    printf("Programming target region... ");
    flash_range_program(FLASH_TARGET_OFFSET, buff, FLASH_DATA_SIZE);
    printf("Done.\n");
}

void RefreshConfigBuff(uint8_t * buff){    
    char tmp_ssid[32];
    memset(tmp_ssid,0x00,sizeof(tmp_ssid));
    sprintf(tmp_ssid,"%s%s",KEY_SSID,WiFiSSID);
    memcpy(buff+OFFSET_SSID,tmp_ssid,sizeof(tmp_ssid));
    char tmp_pass[32];
    memset(tmp_pass,0x00,sizeof(tmp_pass));
    sprintf(tmp_pass,"%s%s",KEY_PASS,WiFiPASS);
    memcpy(buff+OFFSET_PASS,tmp_pass,sizeof(tmp_pass));
    
    FormatFlashConfig();
    flash_range_program(FLASH_TARGET_OFFSET, buff, FLASH_DATA_SIZE);
}
#endif
