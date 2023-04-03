////////////////////////////////////
// - Change the Strings comps to string finds and read the buffer directly?
// - Optimize the ESP_AT.H file! Specially to handle the UDP connections better (fast)
// - Add the mobile_impl_sock_* functions to handle the Request functions (only the necessary for now) - https://discord.com/channels/375413108467957761/541384270636384259/1017548420866654280
// -- Docs about AT commands 
// ---- https://www.espressif.com/sites/default/files/documentation/4a-esp8266_at_instruction_set_en.pdf
// ---- https://github.com/espressif/ESP8266_AT/wiki/ATE
// ---- https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/index.html
// ---- MOBILE_ENABLE_NO32BIT - try to build with this option enabled to handle GBA games
////////////////////////////////////

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/resets.h"
#include "hardware/flash.h"

#include "common.h"
#include "esp_at.h"
#include "flash_eeprom.h"
#include "libmobile_funcs.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS
//#define MOBILE_ENABLE_NO32BIT

//#define ERASE_EEPROM //Encomment this to ERASE ALL stored config (including Adapter config)
//#define CONFIG_MODE //Uncomment this if you want to reconfigure something

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
    unsigned dns_port = MOBILE_DNS_PORT;
    unsigned p2p_port = MOBILE_DEFAULT_P2P_PORT;
    struct mobile_addr relay = {0};
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

    #ifdef CONFIG_MODE
        char newSSID[28] = "Wifi_SSID";
        char newPASS[28] = "PASSW0RD";

        char MAGB_DNS1[60] = "0.0.0.0";
        char MAGB_DNS2[60] = "0.0.0.0";
        unsigned MAGB_DNSPORT = 53;

        char RELAY_SERVER[60] = ".0.0.0.0";
        unsigned P2P_PORT = 1027;
        char RELAY_TOKEN[32] = "00000000000000000000000000000000";
        bool updateRelayToken = false;

        bool DEVICE_UNMETERED = false;

        //Set the new values
        memcpy(WiFiSSID,newSSID,sizeof(newSSID));
        memcpy(WiFiPASS,newPASS,sizeof(newPASS));

        main_parse_addr(&dns1, MAGB_DNS1);
        main_parse_addr(&dns2, MAGB_DNS2);
        main_set_port(&dns1, MAGB_DNSPORT);
        main_set_port(&dns2, MAGB_DNSPORT);

        main_parse_addr(&relay, RELAY_SERVER);
        main_set_port(&relay, MOBILE_DEFAULT_RELAY_PORT);

        //Set the values to the Adapter config
        mobile_config_set_device(mobile->adapter, device, DEVICE_UNMETERED);
        mobile_config_set_dns(mobile->adapter, &dns1, &dns2);
        mobile_config_set_relay(mobile->adapter, &relay);
        mobile_config_set_p2p_port(mobile->adapter, P2P_PORT);

        //ONLY UNCOMMENT THIS LINE IF YOU WANT TO SETUP A TOKEN MANUALLY
        if (updateRelayToken) {
            bool TokenOk = main_parse_hex(relay_token_buf, RELAY_TOKEN, sizeof(relay_token_buf));
            if(!TokenOk){
                printf("Invalid Relay Token\n");
            }else{
                mobile_config_set_relay_token(mobile->adapter, relay_token_buf);
            }
        }

        mobile_config_save(mobile->adapter);
        RefreshConfigBuff(mobile->config_eeprom);

        printf("New configuration defined! Please comment the \'#define CONFIG_MODE\' again to back the adapter to the normal operation.\n");
        while (1){
            LED_ON;
            Delay_Timer(MS(300));
            LED_OFF;
        }
    #endif

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