////////////////////////////////////
// - Change the Strings comps to string finds and read the buffer directly?
// - Optimize the ESP_AT.H file! Specially to handle the UDP connections better (fast)
// - Add the mobile_board_sock_* functions to handle the Request functions (only the necessary for now) - https://discord.com/channels/375413108467957761/541384270636384259/1017548420866654280
// -- Docs about AT commands 
// ---- https://www.espressif.com/sites/default/files/documentation/4a-esp8266_at_instruction_set_en.pdf
// ---- https://github.com/espressif/ESP8266_AT/wiki/ATE
// ---- https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/index.html
////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/resets.h"
//#include "hardware/clocks.h"
#include "hardware/flash.h"

#include "common.h"
#include "esp_at.h"
#include "flash_eeprom.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

// SPI pins
#define SPI_PORT        spi0
#define SPI_BAUDRATE    64 * 1024 * 8
#define PIN_SPI_SIN     16
#define PIN_SPI_SCK     18
#define PIN_SPI_SOUT    19 

//UART pins
#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5

volatile uint64_t time_us_now = 0;
uint64_t last_readable = 0;

#define MAGB_HOST "192.168.0.126"
#define MAGB_PORT 80

#define MAGB_DNS1 "192.168.0.126"
#define MAGB_DNS2 "192.168.0.127"

#define CONFIG_OFFSET_MAGB          0 // Up to 256ytes
#define CONFIG_OFFSET_WIFI_SSID     260 //28bytes (+4 to identify the config, "SSID" in ascii)
#define CONFIG_OFFSET_WIFI_PASS     292 //28bytes (+4 to identify the config, "PASS" in ascii)
#define CONFIG_OFFSET_WIFI_SIZE     28 //28bytes (+4 to identify the config, "SSID" in ascii)
char WiFiSSID[32] = "";
char WiFiPASS[32] = "";
bool isESPDetected = false;

bool haveAdapterConfig = false;
bool haveWifiConfig = false;
bool haveConfigToWrite = false;

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////
// SPI Functions
///////////////////////////////////////

static uint8_t set_initial_data() {
    //Keeping this function if needs to do something during this call 
    return 0xD2;
}

static inline void trigger_spi(spi_inst_t *spi, uint baudrate) {
    //spi_init
    reset_block(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
    unreset_block_wait(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);

    spi_set_baudrate(spi, baudrate);
    spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);
    hw_set_bits(&spi_get_hw(spi)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);
    spi_set_slave(spi, true);
    
    // Set the first data into the buffer
    spi_get_hw(spi)->dr = set_initial_data();

    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);
}

///////////////////////////////////////
// Mobile Adapter GB Functions
///////////////////////////////////////

unsigned long millis_latch = 0;
#define A_UNUSED __attribute__((unused))

void mobile_board_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

void mobile_board_serial_disable(A_UNUSED void *user) {
    spi_deinit(SPI_PORT);
}

void mobile_board_serial_enable(A_UNUSED void *user) {  
    trigger_spi(SPI_PORT,SPI_BAUDRATE);
}

bool mobile_board_config_read(void *user, void *dest, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)mobile->config_eeprom[offset + i];
    }
    return true;
}

bool mobile_board_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        mobile->config_eeprom[offset + i] = ((uint8_t *)src)[i];
    }
    haveConfigToWrite = true;
    last_readable = time_us_64();
    return true;
}

void mobile_board_time_latch(A_UNUSED void *user, enum mobile_timers timer) {
    millis_latch = time_us_64();
}

bool mobile_board_time_check_ms(A_UNUSED void *user, enum mobile_timers timer, unsigned ms) {
    return (time_us_64() - millis_latch) > MS(ms);
}

/*
you don't need to support everything it says right out of the gate, for example IPv6 isn't used currently.
and UDP is only necessary for DNS
similarly the bind() functionality is only used for receiving calls.
so you can just check for all the things you don't support and return errors for now 
This API might not map exactly to the AT commands the ESP has but it should be close-ish
For example, sock_open() should do nothing in your case but initialize a structure that keeps track of the socket's type, and then when sock_connect() is called, AT+CIPSTART needs to be called with the socket type specified in sock_open() and the address specified in sock_connect().
Same for sock_listen() but the AT command would be AT+CIPSERVER
sock_accept() would check if anything has connected to the AT+CIPSERVER socket
All of sock_connect(), sock_listen() and sock_accept() are for TCP
For UDP the destination address is specified in sock_send(), and apparently this mirrors the AT+CIPSENDEX command.
Similarly for TCP connections sock_send() is an AT+CIPSEND command.
sock_recv() would depend on whether you're using active or passive mode for receiving, but I'm seeing there's no passive mode reception command for UDP which might be an issue.
sock_recv() is also a funky one because it needs to signal to libmobile whether the connection has been closed (if it's a TCP connection) and it needs to know where the message came from (especially for UDP sockets) 
*/

bool mobile_board_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_board_sock_open\n");

    if(mobile->esp_sockets[conn].host_id != -1){
        printf("mobile_board_sock_open RETURN FALSE\n");
        return false;
    }

    switch (addrtype) {
        case MOBILE_ADDRTYPE_IPV4:
        case MOBILE_ADDRTYPE_IPV6:
            mobile->esp_sockets[conn].host_iptype = addrtype;
            break;
        default: 
            printf("mobile_board_sock_open RETURN FALSE\n");
            return false;
    }

    switch (socktype) {
        case MOBILE_SOCKTYPE_TCP:
            mobile->esp_sockets[conn].host_type = 1;
            break;
        case MOBILE_SOCKTYPE_UDP: 
            mobile->esp_sockets[conn].host_type = 2;
            break;
        default: 
            printf("mobile_board_sock_open RETURN FALSE\n");
            return false;
    }
    
    mobile->esp_sockets[conn].host_id = conn;
    mobile->esp_sockets[conn].local_port = bindport;

    if(mobile->esp_sockets[conn].host_type == 2 && !mobile->esp_sockets[conn].sock_status){
        mobile->esp_sockets[conn].sock_status = ESP_OpenSockConn(UART_ID, conn, mobile->esp_sockets[conn].host_iptype == MOBILE_ADDRTYPE_IPV4 ? "UDP" : "UDPv6", mobile->esp_sockets[conn].host_iptype == MOBILE_ADDRTYPE_IPV4 ? "127.0.0.1" : "0:0:0:0:0:0:0:1", 1, mobile->esp_sockets[conn].local_port, 2);
        printf("mobile_board_sock_open RETURN %s\n", mobile->esp_sockets[conn].sock_status ? "TRUE":"FALSE");
        return mobile->esp_sockets[conn].sock_status;
    }
    printf("mobile_board_sock_open RETURN TRUE\n");
    return true;    
}

void mobile_board_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_board_sock_close\n");
    if(mobile->esp_sockets[conn].sock_status){
        if(ESP_CloseSockConn(UART_ID, conn)){
            mobile->esp_sockets[conn].host_id = -1;
            //mobile->esp_sockets[conn].local_port = -1;
            mobile->esp_sockets[conn].sock_status = false;
        }
    }
}

int mobile_board_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_board_sock_connect\n");
    
    if(!mobile->esp_sockets[conn].sock_status){
        char srv_ip[46];
        memset(srv_ip,0x00,sizeof(srv_ip));
        int srv_port=0;
        char sock_type[5];

        if (addr->type == MOBILE_ADDRTYPE_IPV4) {
            const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
            sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
            srv_port=addr4->port;
            if (mobile->esp_sockets[conn].host_type == 1) {
                sprintf(sock_type,"TCP");
            }else if(mobile->esp_sockets[conn].host_type == 2){
                sprintf(sock_type,"UDP");
            }else{
                printf("mobile_board_sock_connect RETURN -1\n");
                return -1;
            }
        } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
            const struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
            //Need to parse IPV6
            //sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
            srv_port=addr6->port;
            if (mobile->esp_sockets[conn].host_type == 1) {
                sprintf(sock_type,"TCPv6");
            }else if(mobile->esp_sockets[conn].host_type == 2){
                sprintf(sock_type,"UDPv6");
            }else{
                printf("mobile_board_sock_connect RETURN -1\n");
                return -1;
            } 
            printf("mobile_board_sock_connect RETURN -1\n");
            return -1;
        } else {
            printf("mobile_board_sock_connect RETURN -1\n");
            return -1;
        }

        mobile->esp_sockets[conn].sock_status = ESP_OpenSockConn(UART_ID, conn, sock_type, srv_ip, srv_port, mobile->esp_sockets[conn].local_port, mobile->esp_sockets[conn].host_type == 2 ? 2 : 0);
           
        if(mobile->esp_sockets[conn].sock_status){
            printf("mobile_board_sock_connect RETURN 1\n");
            return 1;
        }
    }else{
        printf("mobile_board_sock_connect RETURN 1\n");
        return 1;
    }
    printf("mobile_board_sock_connect RETURN -1\n");
    return -1;
}

bool mobile_board_sock_listen(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_board_sock_listen\n");
    return false;
}
bool mobile_board_sock_accept(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_board_sock_accept\n");
    return false;
}
 
int mobile_board_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){
    //user = 0x20001ab8
    //conn = 0
    //data = 0x20001dc4 (memory address)
    //                00 01 01 00 00 01 00 00 00 00 00 00       ............
    //    07 67 61 6D 65 62 6F 79 0A 64 61 74 61 63 65 6E   .gameboy.datacen
    //    74 65 72 02 6E 65 02 6A 70 00 00 01 00 01         ter.ne.jp.....
    //size = 42
    //addr = 0x20001da0
    //      Type: MOBILE_ADDRTYPE_IPV4
    //      _addr4 - type:MOBILE_ADDRTYPE_IPV4 / port:53 / host:210 196 3 183)
    //      _addr6 - type:MOBILE_ADDRTYPE_IPV4 / port:53 / host:210 196 3 183 1 9 0 0 53 0 0 0 210 141 112 163)
    
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_board_sock_send\n");

    char srv_ip[46];
    memset(srv_ip,0x00,sizeof(srv_ip));
    int srv_port=0;
    char sock_type[5];
    uint8_t sendDataStatus;

    if(mobile->esp_sockets[conn].host_type == 2){
        if (addr->type == MOBILE_ADDRTYPE_IPV4) {
        const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
        srv_port=addr4->port;
        } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
            const struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
            //Need to parse IPV6
            srv_port=addr6->port;
        }
        sendDataStatus = ESP_SendData(UART_ID, conn, "UDP" , srv_ip, srv_port, data, size);
    }else if(mobile->esp_sockets[conn].host_type == 2){
        sendDataStatus = ESP_SendData(UART_ID, conn, "TCP" , "0.0.0.0", 0, data, 0);
        char checkClose[12];
        sprintf(checkClose,"%i,CLOSED\r\n",conn);
        if (strstr(buffATrx,checkClose) != NULL){
            mobile->esp_sockets[conn].host_id = -1;
            //mobile->esp_sockets[conn].local_port = -1;
            //mobile->esp_sockets[conn].host_iptype = MOBILE_ADDRTYPE_NONE;
            mobile->esp_sockets[conn].sock_status = false;
            FlushATBuff();
        }
    }else{
        printf("mobile_board_sock_send RETURN -1\n");
        return -1;
    }
    printf("mobile_board_sock_send RETURN %i\n", sendDataStatus);
    return sendDataStatus;
}

int mobile_board_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    // Returns: amount of data received on success,
    //          -1 on error,
    //          -2 on remote disconnect

    //user = 0x20001ab8
    //conn = 0
    //data = 0x20001e04 (memory address) << Need to feed
    //size = 512
    //addr = 0x20041f08
    //      Type: MOBILE_ADDRTYPE_NONE
    //      _addr4 - type:MOBILE_ADDRTYPE_NONE / port:0 / host:0 0 0 0)
    //      _addr6 - type:MOBILE_ADDRTYPE_NONE / port:0 / host:0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0)
    
    //? TCP SOCKETS CLOSES AS SOON THE DATA ARRIVES, BUT IT KEEPS THE DATA INTO THE BUFFER
    //? UDP SOCKETS KEEP THE SOCKET OPEN, BUT DON'T STORE THE DATA INTO ANY BUFFER

    // If the Socket is an UDP, Search for a \r\n+IPD,<connID>,<size>: 
    // Feed Data variable with the received size
    // could generate a conflict with the buffer cleaner

    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_board_sock_recv\n");

    int len = -1;

    //Checking if there is any UDP data in the buffer
    if(mobile->esp_sockets[conn].host_type == 2){
        char cmdCheck[100];
        sprintf(cmdCheck,"+IPD,%i,",conn);
        if(ESP_SerialFind(buffATrx,cmdCheck,SEC(5),false)){
            Delay_Timer(MS(500));

            int buffAT_size = sizeof(buffATrx);
            char buffAT_cpy[buffAT_size];
            memset(buffAT_cpy,0x00,sizeof(buffAT_cpy));
            memcpy(buffAT_cpy,buffATrx,buffAT_size);
            FlushATBuff();

            int cmdIndex = ESP_GetCmdIndexBuffer(buffAT_cpy, cmdCheck);
            int lastpointer = 0;
            char numipd[5];

            // Read the ammount of received data
            for(int i = cmdIndex+strlen(cmdCheck); i < buffAT_size; i++){
                if(buffAT_cpy[i] == ','){
                    numipd[i-(lastpointer)] = 0x00;
                    lastpointer = i+1;
                    break;
                }else{
                    numipd[i-(cmdIndex+strlen(cmdCheck))] = buffAT_cpy[i];
                }
            }
            //ipdVal[conn] = atoi(numipd); //Set the IPD value into a variable to control the data to send
            if(atoi(numipd) > 0){
                len = atoi(numipd);
            }
            printf("ESP-01 UDP Bytes Received: %i\n", len);

            // Read the remote IP from the UDP answer        
            //char remoteIP[50];
            //for(int i = lastpointer; i < buffAT_size; i++){
            //    if(buffAT_cpy[i] == ','){
            //        remoteIP[i-(lastpointer)] = 0x00;
            //        lastpointer = i+1;
            //        break;
            //    }else{
            //        remoteIP[i-(lastpointer)] = buffAT_cpy[i];
            //    }
            //}

            // Read the remote Port from the UDP answer  
            char remotePORT[10];
            for(int i = lastpointer; i < buffAT_size; i++){
                if(buffAT_cpy[i] == ':'){
                    remotePORT[i-(lastpointer)] = 0x00;
                    lastpointer = i+1;
                    break;
                }else{
                    remotePORT[i-(lastpointer)] = buffAT_cpy[i];
                }
            }
            int numRemotePort = atoi(remotePORT);
            memcpy(data, buffAT_cpy + lastpointer, len); //memcpy with offset in source
        }
        if(!ESP_GetSockStatus(UART_ID, conn, user)){
            len = -2;
        }
    }else if(mobile->esp_sockets[conn].host_type == 1){
        if(ipdVal[conn] == 0){
            if(ESP_ReadBuffSize(UART_ID,conn) == 0){
                printf("mobile_board_sock_recv RETURN -1\n");
                return -1;
            }
        }

        len = ESP_ReqDataBuff(UART_ID,conn,size);
        memcpy(data,buffTCPReq,len);
    }else{
        len = -1;
    }
    
    printf("mobile_board_sock_recv RETURN %i\n",len);
    return len;
}

///////////////////////////////////////
// Main Functions and Core 1 Loop
///////////////////////////////////////

void core1_context() {
    irq_set_mask_enabled(0xffffffff, false);
    while (true) {
        if (spi_is_readable(SPI_PORT)) {
            spi_get_hw(SPI_PORT)->dr = mobile_transfer(&mobile->adapter, spi_get_hw(SPI_PORT)->dr);
        }
    }
}

void main_parse_addr(struct mobile_addr *dest, char *argv){
    unsigned char ip[MOBILE_PTON_MAXLEN];
    int rc = mobile_pton(MOBILE_PTON_ANY, argv, ip);

    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (rc) {
    case MOBILE_PTON_IPV4:
        dest4->type = MOBILE_ADDRTYPE_IPV4;
        dest4->port = MOBILE_DNS_PORT;
        memcpy(dest4->host, ip, sizeof(dest4->host));
        break;
    case MOBILE_PTON_IPV6:
        dest6->type = MOBILE_ADDRTYPE_IPV6;
        dest6->port = MOBILE_DNS_PORT;
        memcpy(dest6->host, ip, sizeof(dest6->host));
        break;
    //default:
        //fprintf(stderr, "Invalid parameter for %s: %s\n", argv[0], argv[1]);
    }
}

void main(){
    speed_240_MHz = set_sys_clock_khz(240000, false);
    stdio_init_all();

    // For toggle_led
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    LED_ON;

    // Initialize SPI pins
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SCK);
    gpio_set_function(PIN_SPI_SIN, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SIN);
    gpio_set_function(PIN_SPI_SOUT, GPIO_FUNC_SPI), gpio_pull_down(PIN_SPI_SOUT);

    mobile = malloc(sizeof(struct mobile_user));
    struct mobile_adapter_config adapter_config = MOBILE_ADAPTER_CONFIG_DEFAULT;


    main_parse_addr(&adapter_config.dns1, MAGB_DNS1);
    main_parse_addr(&adapter_config.dns2, MAGB_DNS2);
    //adapter_config.p2p_port = 1027
    //adapter_config.device = x
    //adapter_config.unmetered = true;


    memset(WiFiSSID,0x00,sizeof(WiFiSSID));
    memset(WiFiPASS,0x00,sizeof(WiFiSSID));
    memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));

    uint8_t setConfig = ReadFlashConfig(mobile->config_eeprom); 
    switch (setConfig){
        case 0:
            haveAdapterConfig = false;
            haveWifiConfig = false;
        break;
        case 1:
            haveAdapterConfig = true;
            haveWifiConfig = false;
        break;
        case 2:
            haveAdapterConfig = true;
            haveWifiConfig = true;
        break;
        case 3:
            haveAdapterConfig = false;
            haveWifiConfig = true;
        break;
        default:
        break;
    }

    if(haveWifiConfig){
        for(int i = CONFIG_OFFSET_WIFI_SSID; i < CONFIG_OFFSET_WIFI_SSID+CONFIG_OFFSET_WIFI_SIZE; i++){
            WiFiSSID[i-CONFIG_OFFSET_WIFI_SSID] = (char)mobile->config_eeprom[i];
        }
        for(int i = CONFIG_OFFSET_WIFI_PASS; i < CONFIG_OFFSET_WIFI_PASS+CONFIG_OFFSET_WIFI_SIZE; i++){
            WiFiPASS[i-CONFIG_OFFSET_WIFI_PASS] = (char)mobile->config_eeprom[i];
        }
    }else{
        //Set a Default value (need to create a method to configure this setttings later. Maybe a dual boot with a custom GB rom?)
        sprintf(WiFiSSID,"SSID%s","WiFi_Network");
        sprintf(WiFiPASS,"PASS%s","P@$$w0rd");
        for(int i = CONFIG_OFFSET_WIFI_SSID-4; i < CONFIG_OFFSET_WIFI_SSID+CONFIG_OFFSET_WIFI_SIZE; i++){
            mobile->config_eeprom[i] = WiFiSSID[i-CONFIG_OFFSET_WIFI_SSID];
        }
        for(int i = CONFIG_OFFSET_WIFI_PASS-4; i < CONFIG_OFFSET_WIFI_PASS+CONFIG_OFFSET_WIFI_SIZE; i++){
            mobile->config_eeprom[i] = WiFiPASS[i-CONFIG_OFFSET_WIFI_PASS];
        }
    }

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
        LED_OFF;

        mobile->action = MOBILE_ACTION_NONE;
        for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
            mobile->esp_sockets[i].host_id = -1;
            mobile->esp_sockets[i].host_iptype = MOBILE_ADDRTYPE_NONE;
            mobile->esp_sockets[i].host_type = 0;
            mobile->esp_sockets[i].local_port = -1;
            mobile->esp_sockets[i].sock_status = false;
        } 

        mobile_init(&mobile->adapter, mobile, &adapter_config);
        multicore_launch_core1(core1_context);
        FlushATBuff();

        //
        //char dado[60] = "GET /01/CGB-B9AJ/index.php HTTP/1.0\r\nHost: 192.168.0.126\r\n\r\n";
        //uint8_t dadoudp [] = {0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x67,0x61,0x6D,0x65,0x62,0x6F,0x79,0x0A,0x64,0x61,0x74,0x61,0x63,0x65,0x6E,0x74,0x65,0x72,0x02,0x6E,0x65,0x02,0x6A,0x70,0x00,0x00,0x01,0x00,0x01};

        //ESP_OpenSockConn(UART_ID,0,"TCP","192.168.0.126",80,0,0);
        //ESP_OpenSockConn(UART_ID,1,"UDP","192.168.0.126",53,0,2);

        //ESP_SendData(UART_ID,0,"TCP","192.168.0.126",80,dado,60);
        //ESP_SendData(UART_ID,1,"UDP","192.168.0.126",53,dadoudp,sizeof(dadoudp));

        //Delay_Timer(SEC(5));

        //ESP_ReqDataBuff(UART_ID,0,100);

        //ESP_SendData(UART_ID,1,"UDP","192.168.0.126",57318,dado,60);
        //

        while (true) {
            mobile_loop(&mobile->adapter);

            if(haveConfigToWrite){
                time_us_now = time_us_64();
                if (time_us_now - last_readable > SEC(5)){
                    if(!spi_is_readable(SPI_PORT)){
                        multicore_reset_core1();
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
        //do something
    }
}
