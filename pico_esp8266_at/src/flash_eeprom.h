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
#define KEY_PKMUNMETERED "UNME"
#define KEY_DNSPORT "DNSP"
#define KEY_P2PSERVER "P2PS"


#define OFFSET_CONFIG 0
#define OFFSET_MAGB 16
#define OFFSET_SSID 528 //MOBILE_CONFIG_SIZE+OFFSET_SSID
#define OFFSET_PASS 560 //OFFSET_SSID+32
#define OFFSET_DNS1 592 //OFFSET_PASS+32
#define OFFSET_DNS2 656 //OFFSET_DNS1+64
#define OFFSET_P2PPORT 720 //OFFSET_SSID+64
#define OFFSET_PKMUNMETERED 729 //OFFSET_SSID+9
#define OFFSET_DNSPORT 734 //OFFSET_SSID+5
#define OFFSET_P2PSERVER 743 //OFFSET_SSID+5



#define FLASH_TARGET_OFFSET (FLASH_DATA_SIZE * 1024)
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

//256 bytes for the Mobile Adapter GB config and 256 bytes to WiFi Config and other stuffs
void FormatFlashConfig(){
    printf("Erasing target region... ");
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_DATA_SIZE);
    printf("Done.\n");
}

//Read flash memory and set the configs
bool ReadFlashConfig(uint8_t * buff){
    printf("Reading the target region... ");
    memcpy(buff,flash_target_contents,FLASH_DATA_SIZE);
    bool needWrite = false;
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
    //Read DNS1 config (up to 64 bytes)
    if(memmem(buff+OFFSET_DNS1,strlen(KEY_DNS1),KEY_DNS1,strlen(KEY_DNS1)) != NULL){
        memset(MAGB_DNS1,0x00,sizeof(MAGB_DNS1));
        memcpy(MAGB_DNS1,buff+(OFFSET_DNS1+strlen(KEY_DNS1)),64-strlen(KEY_DNS1));        
    }else{
        char tmp_dns1[64];
        memset(tmp_dns1,0x00,sizeof(tmp_dns1));
        sprintf(tmp_dns1,"%s%s",KEY_DNS1,MAGB_DNS1);
        memcpy(buff+OFFSET_DNS1,tmp_dns1,sizeof(tmp_dns1));
        needWrite = true;
    }
    haveDNS1Config = true;
    #endif

    #ifdef USE_CUSTOM_DNS2
    //Read DNS2 config (up to 64 bytes)
    if(memmem(buff+OFFSET_DNS2,strlen(KEY_DNS2),KEY_DNS2,strlen(KEY_DNS2)) != NULL){
        memset(MAGB_DNS2,0x00,sizeof(MAGB_DNS2));
        memcpy(MAGB_DNS2,buff+(OFFSET_DNS2+strlen(KEY_DNS2)),64-strlen(KEY_DNS2));
        
    }else{
        char tmp_dns2[64];
        memset(tmp_dns2,0x00,sizeof(tmp_dns2));
        sprintf(tmp_dns2,"%s%s",KEY_DNS2,MAGB_DNS2);
        memcpy(buff+OFFSET_DNS2,tmp_dns2,sizeof(tmp_dns2));
        needWrite = true;
    }
    haveDNS2Config = true;
    #endif

    //P2P Port have 9 bytes (need parse to INT)
    //Unmetered config have 5 bytes (should receive 1 or 0... transform that to TRUE or FALSE)
    
    #ifdef USE_CUSTOM_DNS_PORT
    //Read custom DNS Port config (up to 9 bytes)
    if(memmem(buff+OFFSET_DNSPORT,strlen(KEY_DNSPORT),KEY_DNSPORT,strlen(KEY_DNSPORT)) != NULL){
        memset(MAGB_DNSPORT,0x00,sizeof(MAGB_DNSPORT));
        memcpy(MAGB_DNSPORT,buff+(OFFSET_DNSPORT+strlen(KEY_DNSPORT)),9-strlen(KEY_DNSPORT));
        
    }else{
        char tmp_dnsport[9];
        memset(tmp_dnsport,0x00,sizeof(tmp_dnsport));
        sprintf(tmp_dnsport,"%s%s",KEY_DNSPORT,MAGB_DNSPORT);
        memcpy(buff+OFFSET_DNSPORT,tmp_dnsport,sizeof(tmp_dnsport));
        needWrite = true;
    }
    #endif
    
    //Read P2P relay server (up to 15 bytes)
    if(memmem(buff+OFFSET_P2PSERVER,strlen(KEY_P2PSERVER),KEY_P2PSERVER,strlen(KEY_P2PSERVER)) != NULL){
        memset(P2P_SERVER,0x00,sizeof(P2P_SERVER));
        memcpy(P2P_SERVER,buff+(OFFSET_P2PSERVER+strlen(KEY_P2PSERVER)),9-strlen(KEY_P2PSERVER));
        
    }else{
        char tmp_dnsport[9];
        memset(tmp_dnsport,0x00,sizeof(tmp_dnsport));
        sprintf(tmp_dnsport,"%s%s",KEY_P2PSERVER,P2P_SERVER);
        memcpy(buff+OFFSET_P2PSERVER,tmp_dnsport,sizeof(tmp_dnsport));
        needWrite = true;
    }

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
#endif
