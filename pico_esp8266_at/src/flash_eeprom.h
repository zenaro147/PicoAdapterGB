#ifndef FLASH_EEPROM_H
#define FLASH_EEPROM_H

#include "common.h"

//Flash reprograming example https://github.com/raspberrypi/pico-examples/blob/master/flash/program/flash_program.c
//https://www.makermatrix.com/blog/read-and-write-data-with-the-pi-pico-onboard-flash/


#define KEY_CONFIG "CONFIG"
#define KEY_MAGB "MA"
#define KEY_SSID "SSID"
#define KEY_PASS "PASS"
#define KEY_DNS1 "DNS1"
#define KEY_DNS2 "DNS2"
#define KEY_P2PPORT "P2PP"
#define KEY_DEVICEUNMETERED "UNME"
#define KEY_DNSPORT "DNSP"
#define KEY_P2PSERVER "P2PS"


#define OFFSET_CONFIG 0
#define OFFSET_MAGB 16
#define OFFSET_SSID 528 //MOBILE_CONFIG_SIZE+OFFSET_SSID
#define OFFSET_PASS 560 //OFFSET_SSID+32
#define OFFSET_DNS1 592 //OFFSET_PASS+32
#define OFFSET_DNS2 656 //OFFSET_DNS1+64
#define OFFSET_P2PPORT 720 //OFFSET_SSID+64
#define OFFSET_DEVICEUNMETERED 729 //OFFSET_SSID+9
#define OFFSET_DNSPORT 734 //OFFSET_SSID+5
#define OFFSET_P2PSERVER 743 //OFFSET_SSID+5

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
        memcpy(varConfig,buff+(offset+strlen(key)),64-strlen(key));        
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

    //Read the Mobile Adapter Config (256 bytes)
    if(memmem(buff+OFFSET_MAGB,strlen(KEY_MAGB),KEY_CONFIG,strlen(KEY_MAGB)) != NULL){
        haveAdapterConfig = true;
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

    #ifdef USE_CUSTOM_DNS1
    haveDNS1Config = ReadConfigOption(buff, OFFSET_DNS1,KEY_DNS1,64,MAGB_DNS1);
    #endif

    #ifdef USE_CUSTOM_DNS2
    haveDNS2Config = ReadConfigOption(buff, OFFSET_DNS2, KEY_DNS2, 64, MAGB_DNS2);
    #endif

    //Read custom DNS Port config (up to 9 bytes)
    #ifdef USE_CUSTOM_DNS_PORT
    haveDNSPConfig = ReadConfigOption(buff, OFFSET_DNSPORT, KEY_DNSPORT, 9, MAGB_DNSPORT);
    #endif
    
    //Read P2P relay server (up to 15 bytes)
    #ifdef USE_RELAY_SERVER
    haveP2PSConfig = ReadConfigOption(buff, OFFSET_P2PSERVER, KEY_P2PSERVER, 19, P2P_SERVER);
    #endif
    
    //Read P2P Port (up to 5 bytes)
    #ifdef USE_CUSTOM_P2P_PORT
    haveP2PPConfig = ReadConfigOption(buff, OFFSET_P2PPORT, KEY_P2PPORT, 9, P2P_PORT);
    #endif
    
    //Read Device Unmetered (1 byte)
    #ifdef USE_CUSTOM_DEVICE_UNMETERED
    haveUNMETConfig = ReadConfigOption(buff, OFFSET_DEVICEUNMETERED, KEY_DEVICEUNMETERED, 5, DEVICE_UNMETERED);
    #endif

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

    char tmp_dns1[64];
    memset(tmp_dns1,0x00,sizeof(tmp_dns1));
    sprintf(tmp_dns1,"%s%s",KEY_DNS1,MAGB_DNS1);
    memcpy(buff+OFFSET_DNS1,tmp_dns1,sizeof(tmp_dns1));

    char tmp_dns2[64];
    memset(tmp_dns2,0x00,sizeof(tmp_dns2));
    sprintf(tmp_dns2,"%s%s",KEY_DNS2,MAGB_DNS2);
    memcpy(buff+OFFSET_DNS2,tmp_dns2,sizeof(tmp_dns2));

    char tmp_dnsport[9];
    memset(tmp_dnsport,0x00,sizeof(tmp_dnsport));
    sprintf(tmp_dnsport,"%s%s",KEY_DNSPORT,MAGB_DNSPORT);
    memcpy(buff+OFFSET_DNSPORT,tmp_dnsport,sizeof(tmp_dnsport));

    char tmp_p2pserver[19];
    memset(tmp_p2pserver,0x00,sizeof(tmp_p2pserver));
    sprintf(tmp_p2pserver,"%s%s",KEY_P2PSERVER,P2P_SERVER);
    memcpy(buff+OFFSET_P2PSERVER,tmp_p2pserver,sizeof(tmp_p2pserver));

    char tmp_p2pport[9];
    memset(tmp_p2pport,0x00,sizeof(tmp_p2pport));
    sprintf(tmp_p2pport,"%s%s",KEY_P2PPORT,P2P_PORT);
    memcpy(buff+OFFSET_P2PPORT,tmp_p2pport,sizeof(tmp_p2pport));

    char tmp_unmetered[5];
    memset(tmp_unmetered,0x00,sizeof(tmp_unmetered));
    sprintf(tmp_unmetered,"%s%s",KEY_DEVICEUNMETERED,DEVICE_UNMETERED);
    memcpy(buff+OFFSET_DEVICEUNMETERED,tmp_unmetered,sizeof(tmp_unmetered));

    FormatFlashConfig();
    flash_range_program(FLASH_TARGET_OFFSET, buff, FLASH_DATA_SIZE);
}
#endif
