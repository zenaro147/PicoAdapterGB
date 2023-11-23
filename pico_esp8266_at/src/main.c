////////////////////////////////////
// - Change the Strings comps to string finds and read the buffer directly?
// - Optimize the ESP_AT.H file! Specially to handle the UDP connections better (fast)
// - Request functions discussion - https://discord.com/channels/375413108467957761/541384270636384259/1017548420866654280
// - MOBILE_ENABLE_NO32BIT - try to build with this option enabled to handle GBA games
// -- Docs about AT commands 
// ---- https://www.espressif.com/sites/default/files/documentation/4a-esp8266_at_instruction_set_en.pdf
// ---- https://github.com/espressif/ESP8266_AT/wiki/ATE
// ---- https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/index.html
////////////////////////////////////

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

#include "common.h"
#include "esp_at.h"
#include "flash_eeprom.h"
#include "libmobile_func.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////
// Main Functions and Core 1 Loop
///////////////////////////////////////

void core1_context() {
    irq_set_mask_enabled(0xffffffff, false);

    while (true) {
        if(spi_is_readable(SPI_PORT)){
            spiLock = true;
            //spi_get_hw(SPI_PORT)->dr = mobile_transfer(&mobile->adapter, spi_get_hw(SPI_PORT)->dr);
            if(!is32bitsMode){
               spi_get_hw(SPI_PORT)->dr = mobile_transfer(mobile->adapter, spi_get_hw(SPI_PORT)->dr);
            }else{
                // Mario Kart Data Sample
                // 0x99661900  0xD2D2D2D2  // Start signal, command 0x19 (read eeprom)
                // 0x00020080  0xD2D2D2D2  // Packet size 2, data = [0x00, 0x80] (read from offset 0, 0x80 bytes)
                // 0x0000009B  0xD2D2D2D2  // Padding, 2-byte checksum (0x009B)
                // 0x81000000  0x88990000  // Device ID (0x81 = GBA, 0x88 = Blue adapter), acknowledgement (0x19 ^ 0x80) = 0x99 for current command

                buff32[buff32_pointer++] = spi_get_hw(SPI_PORT)->dr;
                if(buff32_pointer >= 4){
                    #ifdef DEBUG_SIGNAL_PINS
                    gpio_put(9, true);
                    #endif
                    uint8_t tmpbuff[4];
                    //for(int x = 0 ; x < 4 ; x++){                   
                    //    spi_get_hw(SPI_PORT)->dr = mobile_transfer(&mobile->adapter, buff32[x]);  
                    //}

                    //for(int x = 0 ; x < 4 ; x++){                   
                    //    tmpbuff[x] = mobile_transfer(&mobile->adapter, buff32[3-x]);
                    //}
                    //for(int x = 0 ; x < 4 ; x++){                   
                    //    spi_get_hw(SPI_PORT)->dr = tmpbuff[3-x];
                    //}

                    //buff32[0] = spi_get_hw(SPI_PORT)->dr;
                    //buff32[1] = spi_get_hw(SPI_PORT)->dr;
                    //buff32[2] = spi_get_hw(SPI_PORT)->dr;
                    //buff32[3] = spi_get_hw(SPI_PORT)->dr;
                    tmpbuff[0] = mobile_transfer(mobile->adapter, buff32[0]);
                    tmpbuff[1] = mobile_transfer(mobile->adapter, buff32[1]);
                    tmpbuff[2] = mobile_transfer(mobile->adapter, buff32[2]);
                    tmpbuff[3] = mobile_transfer(mobile->adapter, buff32[3]);
                    spi_get_hw(SPI_PORT)->dr = tmpbuff[0];
                    spi_get_hw(SPI_PORT)->dr = tmpbuff[1];
                    spi_get_hw(SPI_PORT)->dr = tmpbuff[2];
                    spi_get_hw(SPI_PORT)->dr = tmpbuff[3];
                    buff32_pointer -= 4;
                    #ifdef DEBUG_SIGNAL_PINS
                    gpio_put(9, false);
                    #endif
                }
            }
            spiLock = false;
        }
    }
}

void mobile_validate_relay(){
    struct mobile_addr relay = {0};    
    mobile_config_get_relay(mobile->adapter, &relay);
    if (relay.type != MOBILE_ADDRTYPE_NONE){
            busy_wait_us(MS(150));
            LED_ON;
            busy_wait_us(MS(150));
            LED_OFF;
            busy_wait_us(MS(150));
            LED_ON;
            busy_wait_us(MS(150));
            LED_OFF;
            busy_wait_us(MS(150));
            LED_ON;
    }
}

void main(){
    //Setup Pico
    speed_240_MHz = set_sys_clock_khz(240000, false);
    stdio_init_all();

    #ifdef DEBUG_SIGNAL_PINS
    gpio_init(9);
    gpio_set_dir(9, GPIO_OUT);
    gpio_put(9, false);

    gpio_init(10);
    gpio_set_dir(10, GPIO_OUT);
    gpio_put(10, false);
    #endif

    // For toggle_led
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    LED_ON;

    // Initialize SPI pins
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SCK);
    gpio_set_function(PIN_SPI_SIN, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_SOUT, GPIO_FUNC_SPI);

    //Libmobile Variables
    enum mobile_adapter_device device = MOBILE_ADAPTER_BLUE;
    bool device_unmetered = false;
    struct mobile_addr dns1 = {0};
    struct mobile_addr dns2 = {0};
    unsigned dns_port = -1;
    unsigned p2p_port = MOBILE_DEFAULT_P2P_PORT;
    struct mobile_addr relay = {0};
    bool relay_token_update = false;
    unsigned char *relay_token = NULL;
    unsigned char relay_token_buf[MOBILE_RELAY_TOKEN_SIZE];

    mobile = malloc(sizeof(struct mobile_user));

    #ifdef ERASE_EEPROM
    FormatFlashConfig();
    #endif

    memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));
    ReadFlashConfig(mobile->config_eeprom); 

    // Initialize mobile library
    mobile->adapter = mobile_new(mobile);
    mobile_def_debug_log(mobile->adapter, impl_debug_log);
    mobile_def_serial_disable(mobile->adapter, impl_serial_disable);
    mobile_def_serial_enable(mobile->adapter, impl_serial_enable);
    mobile_def_config_read(mobile->adapter, impl_config_read);
    mobile_def_config_write(mobile->adapter, impl_config_write);
    mobile_def_time_latch(mobile->adapter, impl_time_latch);
    mobile_def_time_check_ms(mobile->adapter, impl_time_check_ms);
    mobile_def_sock_open(mobile->adapter, impl_sock_open);
    mobile_def_sock_close(mobile->adapter, impl_sock_close);
    mobile_def_sock_connect(mobile->adapter, impl_sock_connect);
    mobile_def_sock_listen(mobile->adapter, impl_sock_listen);
    mobile_def_sock_accept(mobile->adapter, impl_sock_accept);
    mobile_def_sock_send(mobile->adapter, impl_sock_send);
    mobile_def_sock_recv(mobile->adapter, impl_sock_recv);
    mobile_def_update_number(mobile->adapter, impl_update_number);

    mobile_config_load(mobile->adapter);
   
    //CONFIG MODE
    int UserInput;
    printf("Press any key to enter in Setup Mode...\n");
    UserInput = getchar_timeout_us(SEC(5));
    if(UserInput != PICO_ERROR_TIMEOUT){
        char UserCMD[512] = {0};
        bool needSave = false;

        mobile_config_get_dns(mobile->adapter, &dns1, MOBILE_DNS1);
        mobile_config_get_dns(mobile->adapter, &dns2, MOBILE_DNS2);
        mobile_config_get_relay(mobile->adapter, &relay);

        while(1){
            printf("Enter a command: \n");
            scanf("%512s",UserCMD);

            //Set the new WiFi SSID
            if(FindCommand(UserCMD,"WIFISSID=")){
                if(strlen(UserCMD)-9 > 0){
                    memset(WiFiSSID,0x00,sizeof(WiFiSSID));
                    memcpy(WiFiSSID,UserCMD+9,strlen(UserCMD)-9);
                    printf("New WiFi SSID defined.\n");
                    needSave=true;  
                }else if(strlen(UserCMD)-9 == 0){
                    memcpy(WiFiSSID,"WiFi_Network",12);
                    printf("Default WiFi SSID defined.\n");
                    needSave=true;  
                }else{
                    printf("Invalid parameter.\n");
                }

            //Set the new WiFi Password
            }else if(FindCommand(UserCMD,"WIFIPASS=")){
                if(strlen(UserCMD)-9 > 0){
                    memset(WiFiPASS,0x00,sizeof(WiFiPASS));
                    memcpy(WiFiPASS,UserCMD+9,strlen(UserCMD)-9);
                    printf("New WiFi Password defined.\n");
                    needSave=true;
                }else if(strlen(UserCMD)-9 == 0){
                    memcpy(WiFiPASS,"P@$$w0rd",8);
                    printf("Default WiFi Password defined.\n");
                    needSave=true;
                }else{
                    printf("Invalid parameter.\n");
                }
            
            //Set the new DNS1 for Libmobile
            }else if(FindCommand(UserCMD,"DNS1=")){
                if(strlen(UserCMD)-5 > 0){
                    char MAGB_DNS[60] = {0};
                    memcpy(MAGB_DNS,UserCMD+5,strlen(UserCMD)-5);
                    int ValidDNS = main_parse_addr(&dns1, MAGB_DNS);
                    if(ValidDNS == 1){
                        mobile_config_set_dns(mobile->adapter, &dns1, MOBILE_DNS1);
                        printf("New DNS Server 1 defined.\n");
                        needSave=true;
                    } 
                }else if(strlen(UserCMD)-5 == 0){
                      mobile_config_set_dns(mobile->adapter, &(struct mobile_addr){.type=MOBILE_ADDRTYPE_NONE}, MOBILE_DNS1);
                      printf("Default DNS Server 1 defined.\n");
                      needSave=true;
                }else{
                    printf("Invalid parameter.\n");
                }

            //Set the new DNS2 for Libmobile
            }else if(FindCommand(UserCMD,"DNS2=")){
                if(strlen(UserCMD)-5 > 0){
                    char MAGB_DNS[60] = {0};
                    memcpy(MAGB_DNS,UserCMD+5,strlen(UserCMD)-5);
                    int ValidDNS = main_parse_addr(&dns2, MAGB_DNS);
                    if(ValidDNS == 1){
                        mobile_config_set_dns(mobile->adapter, &dns2, MOBILE_DNS2);
                        printf("New DNS Server 2 defined.\n");
                        needSave=true;
                    } 
                }else if(strlen(UserCMD)-5 == 0){
                      mobile_config_set_dns(mobile->adapter, &(struct mobile_addr){.type=MOBILE_ADDRTYPE_NONE}, MOBILE_DNS2);
                      printf("Default DNS Server 2 defined.\n");
                      needSave=true;
                }else{
                    printf("Invalid parameter.\n");
                } 
            
            //Set the new DNS Port for Libmobile
            }else if(FindCommand(UserCMD,"DNSPORT=")){
                if(strlen(UserCMD)-8 > 0){
                    char dnsportstr[6] = {0};
                    memcpy(dnsportstr,UserCMD+8,strlen(UserCMD)-8);
                    if(atoi(dnsportstr) > 0){
                        dns_port = atoi(dnsportstr);
                        printf("New DNS Server port defined.\n");
                    }else{
                        printf("Invalid parameter.\n");
                    }
                    needSave=true;
                }else if(strlen(UserCMD)-8 == 0){
                    dns_port = MOBILE_DNS_PORT;
                    printf("Default DNS Server port defined.\n");
                    needSave=true;
                }else{
                    printf("Invalid parameter.\n");
                } 
            
            //Set the new Relay Server IP for Libmobile               
            }else if(FindCommand(UserCMD,"RELAYSERVER=")){
                if(strlen(UserCMD)-12 > 0){
                    char RELAY_SERVER[60] = {0};
                    memcpy(RELAY_SERVER,UserCMD+12,strlen(UserCMD)-12);
                    int ValidRelay = main_parse_addr(&relay, RELAY_SERVER);
                    if(ValidRelay == 1){
                        main_set_port(&relay, MOBILE_DEFAULT_RELAY_PORT);
                        mobile_config_set_relay(mobile->adapter, &relay);
                        printf("New Relay Server Address defined.\n");
                        needSave=true;
                    } 
                }else if(strlen(UserCMD)-12 == 0){
                    mobile_config_set_relay(mobile->adapter, &(struct mobile_addr){.type=MOBILE_ADDRTYPE_NONE});
                    printf("Relay Server Address removed.\n");
                    needSave=true;
                }else{
                    printf("Invalid parameter.\n");
                } 

            //Set the new Relay Token for Libmobile                    
            }else if(FindCommand(UserCMD,"RELAYTOKEN=")){
                if(strlen(UserCMD)-11 > 0){
                    char RELAY_TOKEN[33] = {0};
                    memcpy(RELAY_TOKEN,UserCMD+11,32);
                    bool TokenOk = main_parse_hex(relay_token_buf, RELAY_TOKEN, sizeof(relay_token_buf));
                    if(!TokenOk){
                        printf("Invalid parameter.\n");
                    }else{
                        mobile_config_set_relay_token(mobile->adapter, relay_token_buf);
                        printf("New Relay Token defined.\n");
                        needSave=true;
                    }
                }else if(strlen(UserCMD)-11 == 0){
                    mobile_config_set_relay_token(mobile->adapter, NULL);
                    printf("Relay Token removed.\n");
                    needSave=true;
                }else{
                    printf("Invalid parameter.\n");
                } 

            //Set the new P2P Port for Libmobile 
            }else if(FindCommand(UserCMD,"P2PPORT=")){
                if(strlen(UserCMD)-8 > 0){
                    char p2pportstr[6] = {0};                    
                    memcpy(p2pportstr,UserCMD+8,strlen(UserCMD)-8);
                    if(atoi(p2pportstr) > 0){
                        mobile_config_set_p2p_port(mobile->adapter, atoi(p2pportstr));
                        printf("New P2P Port defined.\n");
                    }else{
                        printf("Invalid parameter.\n");
                    }
                    needSave=true;
                }else if(strlen(UserCMD)-8 == 0){
                    mobile_config_set_p2p_port(mobile->adapter, MOBILE_DEFAULT_P2P_PORT);
                    printf("Default P2P Port defined.\n");
                    needSave=true;
                }else{
                    printf("Invalid parameter.\n");
                } 
            
            //Set the new Unmetered setting for Libmobile 
            }else if(FindCommand(UserCMD,"UNMETERED=")){
                if(strlen(UserCMD)-10 == 1){
                    bool unmeteredcheck=false;
                    switch (UserCMD[10]){
                        case '0':
                            unmeteredcheck=false;
                            break;
                        case '1':
                            unmeteredcheck=true;
                            break;
                        default:
                            printf("Invalid parameter. Applying default value.\n");
                            break;
                    }
                    mobile_config_set_device(mobile->adapter, device, unmeteredcheck);
                    printf("New Unmetered value defined.\n");
                    needSave=true;
                }else if(strlen(UserCMD)-10 == 0){
                    mobile_config_set_device(mobile->adapter, device, false);
                    printf("Default Unmetered value defined.\n");
                    needSave=true;
                }else{
                    printf("Invalid parameter.\n");
                } 
            
            //Format the entire EEPROM, if necessary
            }else if(FindCommand(UserCMD,"FORMAT_EEPROM")){
                printf("Formatting...\n");
                memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));

                mobile_config_set_dns(mobile->adapter, &(struct mobile_addr){.type=MOBILE_ADDRTYPE_NONE}, MOBILE_DNS1);
                mobile_config_set_dns(mobile->adapter, &(struct mobile_addr){.type=MOBILE_ADDRTYPE_NONE}, MOBILE_DNS2);
                mobile_config_set_relay(mobile->adapter, &(struct mobile_addr){.type=MOBILE_ADDRTYPE_NONE});
                mobile_config_set_relay_token(mobile->adapter, NULL);
                mobile_config_set_p2p_port(mobile->adapter, MOBILE_DEFAULT_P2P_PORT);
                mobile_config_set_device(mobile->adapter, device, false);
                
                mobile_config_save(mobile->adapter);

                mobile_config_get_dns(mobile->adapter, &dns1, MOBILE_DNS1);
                mobile_config_get_dns(mobile->adapter, &dns2, MOBILE_DNS2);
                mobile_config_get_relay(mobile->adapter, &relay);
                
                memset(WiFiSSID,0x00,sizeof(WiFiSSID));
                memset(WiFiPASS,0x00,sizeof(WiFiPASS));
                strcpy(WiFiSSID,"WiFi_Network");
                strcpy(WiFiPASS,"P@$$w0rd");

                RefreshConfigBuff(mobile->config_eeprom,WiFiSSID,WiFiPASS);

                printf("Device formatted!\n");
                needSave=true;

            //Exit from Menu
            }else if(FindCommand(UserCMD,"EXIT")){
                break;

            // Shows the actual device configuration
            }else if(FindCommand(UserCMD,"SHOW_CONFIG")){
                printf("NETWORK SETTINGS\nWiFi SSID: %s\nWiFi Password: %s\n\nADAPTER SETTINGS\n",WiFiSSID,WiFiPASS);

                char tmpSrv[60] = {0};
                parse_addr_string(&dns1,tmpSrv);
                printf("DNS Server 1: %s\n", tmpSrv);
                parse_addr_string(&dns2,tmpSrv);
                printf("DNS Server 2: %s\n", tmpSrv);
                parse_addr_string(&relay,tmpSrv);
                printf("Relay Server: %s\n", tmpSrv);

                char tmpToken[16] = {0};
                mobile_config_get_relay_token(mobile->adapter,tmpToken);
                printf("Relay Token: ");
                for (int i = 0; i < sizeof(tmpToken); i++){
                    printf("%02hhX", tmpToken[i]);
                }
                printf("\n");

                unsigned tmpPort = 0;
                mobile_config_get_p2p_port(mobile->adapter,&tmpPort);
                printf("P2P Port: %i\n", tmpPort);

                bool tmpUnmet = false;
                mobile_config_get_device(mobile->adapter,NULL,&tmpUnmet);
                printf("Is Unmetered: %s\n\n", tmpUnmet == true ? "Yes":"No");

            }else if(FindCommand(UserCMD,"HELP")){
                printf("Command Sintax: <COMMAND>=<VALUE>\n");
                printf("To reset/clear a parameter, leave the <VALUE> blank, for example: WIFISSID=\n");
                printf("All commands are Case-Sensitive\n\n");
                printf("Command List:\n");
                printf("WIFISSID      | Set the SSID to use to connect.\n");
                printf("WIFIPASS      | Set the password from the WiFi network to use to connect.\n");
                printf("DNS1          | Set primary DNS that the adapter will use to parse the nameservers.\n");
                printf("DNS2          | Set secondary DNS that the adapter will use to parse the nameservers.\n");
                printf("DNSPORT       | Set a custom DNS port to use with the DNS servers.\n");
                printf("RELAYSERVER   | Set a Relay Server that will be use during P2P communications.\n");
                printf("RELAYTOKEN    | Set a Relay Token that will be used on Relay Server to receive a valid number to use during P2P communications.\n");
                printf("P2PPORT       | Set a custom P2P port to use during P2P communications (Local Network only).\n");
                printf("UNMETERED     | Set if the device will be Unmetered (useful for Pokemon Crystal). Only accept 1 (true) or 0 (false).\n\n");

                printf("Special commands (just enter the command, without =<VALUE>):\n");
                printf("FORMAT_EEPROM | Format the eeprom, if necessary.\n");
                printf("SHOW_CONFIG   | Show the actual device configuration.\n");
                printf("EXIT          | Quit from Config Mode and Save the new values. If you change some value, the device will reboot.\n");
                printf("HELP          | Show this command list on screen.\n\n");
                
            //Generic error return
            }else{
                printf("Invalid command.\n");
            }
        }

        if(needSave){
            printf("Saving new configs...\n");
            //Saving the new DNS port
            if(dns_port > 0){
                main_set_port(&dns1, dns_port);
                main_set_port(&dns2, dns_port);
            }else{
                main_set_port(&dns1, MOBILE_DNS_PORT);
                main_set_port(&dns2, MOBILE_DNS_PORT);
            }
            
            //Save new Configs
            mobile_config_save(mobile->adapter);
            RefreshConfigBuff(mobile->config_eeprom,WiFiSSID,WiFiPASS);

            busy_wait_us(SEC(1));

            printf("Please reboot the device...\n");
            while(true){
                LED_ON;
                busy_wait_us(MS(300));
                LED_OFF;
                busy_wait_us(MS(300));
            }
        }
    }
    printf("Continuing initialization...\n");

    //////////////////////
    // CONFIGURE THE ESP
    //////////////////////
    isESPDetected = EspAT_Init(UART_ID, BAUD_RATE, UART_TX_PIN, UART_RX_PIN);
    if(isESPDetected){
        isConnectedWiFi = ESP_ConnectWifi(UART_ID, WiFiSSID, WiFiPASS,SEC(10));
    }
    //////////////////
    // END CONFIGURE
    //////////////////
    
    if(isConnectedWiFi){
        mobile->action = MOBILE_ACTION_NONE;
        mobile->number_user[0] = '\0';
        mobile->number_peer[0] = '\0';
        for (int i = 0; i < MOBILE_MAX_TIMERS; i++) mobile->esp_clock_latch[i] = 0;
        for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
            mobile->esp_sockets[i].host_id = -1;
            mobile->esp_sockets[i].host_iptype = MOBILE_ADDRTYPE_NONE;
            mobile->esp_sockets[i].host_type = 0;
            mobile->esp_sockets[i].local_port = -1;
            mobile->esp_sockets[i].sock_status = false;
            ESP_CloseSockConn(UART_ID,i);
        }

        multicore_launch_core1(core1_context);
        FlushATBuff();

        mobile_start(mobile->adapter);

        LED_OFF;

        mobile_validate_relay();

        while (true) {
            mobile_loop(mobile->adapter);

            if(haveConfigToWrite){
                time_us_now = time_us_64();
                if (time_us_now - last_readable > SEC(5)){
                    if(!spi_is_readable(SPI_PORT)){
                        multicore_reset_core1();
                        FormatFlashConfig();
                        SaveFlashConfig(mobile->config_eeprom);
                        haveConfigToWrite = false;
                        time_us_now = 0;
                        last_readable = 0;                    
                        multicore_launch_core1(core1_context);
                    }else{
                        last_readable = time_us_now;
                    }
                }
            }
        }
    }else{
        printf("Error during Pico setup! =( \n");
        while(true){
            //do something
        }
    }
}
