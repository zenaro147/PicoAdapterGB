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

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS
//#define MOBILE_ENABLE_NO32BIT

//#define ERASE_EEPROM //Encomment this to ERASE ALL stored config (including Adapter config)
//#define CONFIG_MODE //Uncomment this if you want to reconfigure something

// SPI pins
#define SPI_PORT        spi0
#define SPI_BAUDRATE_512    64 * 1024 * 8
#define SPI_BAUDRATE_256    32 * 1024 * 8
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

bool isESPDetected = false;
bool haveConfigToWrite = false;
bool isServerOpened = false;
bool is32bitsMode = false;

uint8_t buff32[4];
volatile int8_t buff32_pointer = 0;
volatile bool spiLock = false;
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////
// SPI Functions
///////////////////////////////////////

static inline void trigger_spi(spi_inst_t *spi, uint baudrate) {
    //spi_init
    buff32_pointer = 0;
    reset_block(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
    unreset_block_wait(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);

    spi_set_baudrate(spi, baudrate);    
    spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);
    hw_set_bits(&spi_get_hw(spi)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);
    spi_set_slave(spi, true);
    
    // Set the first data into the buffer
    if(!is32bitsMode){
        spi_get_hw(spi)->dr = 0xD2;
    }else{
        spi_get_hw(spi)->dr = 0xD2;
        spi_get_hw(spi)->dr = 0xD2;
        spi_get_hw(spi)->dr = 0xD2;
        spi_get_hw(spi)->dr = 0xD2;
    } 

    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);
}

///////////////////////////////////////
// Mobile Adapter GB Functions
///////////////////////////////////////

unsigned long millis_latch = 0;
#define A_UNUSED __attribute__((unused))

////////////////////////
// Auxiliar functions
////////////////////////

void main_parse_addr(struct mobile_addr *dest, char *argv){
    unsigned char ip[MOBILE_INET_PTON_MAXLEN];
    int rc = mobile_inet_pton(MOBILE_INET_PTON_ANY, argv, ip);

    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (rc) {
        case MOBILE_INET_PTON_IPV4:
            dest4->type = MOBILE_ADDRTYPE_IPV4;
            memcpy(dest4->host, ip, sizeof(dest4->host));
            break;
        case MOBILE_INET_PTON_IPV6:
            dest6->type = MOBILE_ADDRTYPE_IPV6;
            memcpy(dest6->host, ip, sizeof(dest6->host));
            break;
        default:
            break;
    }
}

static bool main_parse_hex(unsigned char *buf, char *str, unsigned size){
    unsigned char x = 0;
    for (unsigned i = 0; i < size * 2; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
        else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
        else return false;

        x <<= 4;
        x |= c;

        if (i % 2 == 1) {
            buf[i / 2] = x;
            x = 0;
        }
    }
    return true;
}

void main_set_port(struct mobile_addr *dest, unsigned port){
    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (dest->type) {
    case MOBILE_ADDRTYPE_IPV4:
        dest4->port = port;
        break;
    case MOBILE_ADDRTYPE_IPV6:
        dest6->port = port;
        break;
    default:
        break;
    }
}

/////////////////////////
// Libmobile Functions
/////////////////////////

static void impl_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

static void impl_serial_disable(void *user) {
    #ifdef DEBUG_SIGNAL_PINS
    gpio_put(10, true);
    #endif
    (void)user;
    struct mobile_user *mobile = (struct mobile_user *)user;
    while(spiLock);

    spi_deinit(SPI_PORT);    
}

static void impl_serial_enable(void *user, bool mode_32bit) {
    (void)mode_32bit;
    struct mobile_user *mobile = (struct mobile_user *)user;
    //is32bitsMode = mode_32bit;
    //if(mode_32bit){
    //    trigger_spi(SPI_PORT,SPI_BAUDRATE_512);
    //}else{
        trigger_spi(SPI_PORT,SPI_BAUDRATE_256);
    //}
    #ifdef DEBUG_SIGNAL_PINS
    gpio_put(10, false);
    #endif
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)mobile->config_eeprom[OFFSET_MAGB + offset + i];
    }
    return true;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        mobile->config_eeprom[OFFSET_MAGB + offset + i] = ((uint8_t *)src)[i];
    }
    haveConfigToWrite = true;
    last_readable = time_us_64();
    return true;
}

static void impl_time_latch(A_UNUSED void *user, unsigned timer) {
    millis_latch = time_us_64();
}

static bool impl_time_check_ms(A_UNUSED void *user, unsigned timer, unsigned ms) {
    return (time_us_64() - millis_latch) > MS(ms);
}

static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    // Returns: true if socket was created successfully, false on error
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_open\n");
    FlushATBuff();

    if(mobile->esp_sockets[conn].host_id != -1 && !mobile->esp_sockets[conn].sock_status){
        mobile->esp_sockets[conn].host_id = -1;
        return false;
    }

    switch (addrtype) {
        case MOBILE_ADDRTYPE_IPV4:
        case MOBILE_ADDRTYPE_IPV6:
            mobile->esp_sockets[conn].host_iptype = addrtype;
            break;
        default: 
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
            return false;
    }
    
    mobile->esp_sockets[conn].host_id = conn;
    mobile->esp_sockets[conn].local_port = bindport;

    if(mobile->esp_sockets[conn].host_type == 2 && !mobile->esp_sockets[conn].sock_status){
        bool checkUDPsock = false;
        checkUDPsock = ESP_OpenSockConn(UART_ID, conn, mobile->esp_sockets[conn].host_iptype == MOBILE_ADDRTYPE_IPV4 ? "UDP" : "UDPv6", mobile->esp_sockets[conn].host_iptype == MOBILE_ADDRTYPE_IPV4 ? "127.0.0.1" : "0:0:0:0:0:0:0:1", 1, mobile->esp_sockets[conn].local_port, 2);
        if(!checkUDPsock){
            mobile->esp_sockets[conn].host_id = -1;
            mobile->esp_sockets[conn].local_port = -1;
            mobile->esp_sockets[conn].sock_status = false;
        }else{
            mobile->esp_sockets[conn].sock_status = checkUDPsock;
        }
        return checkUDPsock;
    }
    return true;
}

static void impl_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_close\n");
    if(mobile->esp_sockets[conn].sock_status){
        if(ESP_GetSockStatus(UART_ID,conn,user)){
            ESP_CloseSockConn(UART_ID, conn);
            mobile->esp_sockets[conn].host_id = -1;
            mobile->esp_sockets[conn].local_port = -1;
            mobile->esp_sockets[conn].sock_status = false;
        }
    }
    if(isServerOpened){
        printf("mobile_impl_sock_close - Server\n");
        ESP_CloseServer(UART_ID);
        isServerOpened = false;
    }
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    // Returns: 1 on success, 0 if connect is in progress, -1 on error
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_connect\n");

    if(mobile->esp_sockets[conn].host_id == -1 && !mobile->esp_sockets[conn].sock_status){
        return -1;
    }

    FlushATBuff();

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
                return -1;
            } 
            return -1;
        } else {
            return -1;
        }

        mobile->esp_sockets[conn].sock_status = ESP_OpenSockConn(UART_ID, conn, sock_type, srv_ip, srv_port, mobile->esp_sockets[conn].local_port, mobile->esp_sockets[conn].host_type == 2 ? 2 : 0);
           
        if(mobile->esp_sockets[conn].sock_status){
            return 1;
        }
    }else{
        return 1;
    }
    return -1;
}

static bool impl_sock_listen(void *user, unsigned conn){
    // Returns: true if a connection was accepted,
    //          false if there's no incoming connections    
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_listen\n");
    bool sockP2Pcheck = false;
    if(!isServerOpened && mobile->esp_sockets[conn].host_type == 1){
        sockP2Pcheck = ESP_OpenServer(UART_ID,conn,mobile->esp_sockets[conn].local_port);
        isServerOpened = sockP2Pcheck;
    }
    return sockP2Pcheck;
}
static bool impl_sock_accept(void *user, unsigned conn){
    // Returns: true if a connection was accepted,
    //          false if there's no incoming connections
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_accept\n");
    bool sockP2Pconn = false;
    if(isServerOpened){
        sockP2Pconn = ESP_CheckIncommingConn(UART_ID,conn);
        if(sockP2Pconn){
            mobile->esp_sockets[conn].sock_status = true;
        }
    }    
    return sockP2Pconn;
}
 
static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){    
    // Returns: non-negative amount of data sent on success, -1 on error
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_send\n");
    
    if(mobile->esp_sockets[conn].host_id == -1 && !mobile->esp_sockets[conn].sock_status){
        return -1;
    }
    
    char srv_ip[46];
    memset(srv_ip,0x00,sizeof(srv_ip));
    int srv_port=0;
    char sock_type[5];
    uint8_t sendDataStatus;

    FlushATBuff();
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
    }else if(mobile->esp_sockets[conn].host_type == 1){
        sendDataStatus = ESP_SendData(UART_ID, conn, "TCP" , "0.0.0.0", 0, data, size);
        char checkClose[12];
        sprintf(checkClose,"%i,CLOSED\r\n",conn);
        if (strstr(buffATrx,checkClose) != NULL){
            mobile->esp_sockets[conn].host_id = -1;
            mobile->esp_sockets[conn].local_port = -1;
            //mobile->esp_sockets[conn].host_iptype = MOBILE_ADDRTYPE_NONE;
            mobile->esp_sockets[conn].sock_status = false;
            FlushATBuff();
        }
    }else{
        return -1;
    }
    if(sendDataStatus >= 0){
        return sendDataStatus;
    }else{
        return -1;
    }
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    // Returns: amount of data received on success,
    //          -1 on error,
    //          -2 on remote disconnect
    printf("mobile_impl_sock_recv\n");

    struct mobile_user *mobile = (struct mobile_user *)user;
    struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
    struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
    
    //if(mobile->esp_sockets[conn].host_id == -1 && !mobile->esp_sockets[conn].sock_status && mobile->esp_sockets[conn].host_iptype == MOBILE_ADDRTYPE_NONE){
    //    return -1;
    //}

    int len = -1;
    int numRemotePort=-1;
    char remoteIP[50] = {0};

    //Checking if there is any UDP data in the buffer
    if(mobile->esp_sockets[conn].host_type == 2){
        char cmdCheck[10];
        sprintf(cmdCheck,"+IPD,%i,",conn);
        if(ESP_SerialFind(buffATrx,cmdCheck,SEC(5),false,false)){
            Delay_Timer(MS(300));

            int buffAT_size = sizeof(buffATrx);
            char buffAT_cpy[buffAT_size];
            memset(buffAT_cpy,0x00,sizeof(buffAT_cpy));
            memcpy(buffAT_cpy,buffATrx,buffAT_size);

            int cmdIndex = ESP_GetCmdIndexBuffer(buffAT_cpy, cmdCheck);
            int lastpointer = 0;
            char numipd[5];

            // Read the ammount of received data
            for(int i = cmdIndex+strlen(cmdCheck); i < buffAT_size; i++){
                if(buffAT_cpy[i] == ','){
                    numipd[i-(cmdIndex+strlen(cmdCheck))] = 0x00;
                    lastpointer = i+1;
                    break;
                }else{
                    numipd[i-(cmdIndex+strlen(cmdCheck))] = buffAT_cpy[i];
                }
            }
            if(atoi(numipd) > 0) len = atoi(numipd);
            printf("ESP-01 UDP Bytes Received: %i\n", len);

            // Read the remote IP from the UDP answer  
            for(int i = lastpointer; i < buffAT_size; i++){
                if(buffAT_cpy[i] == ','){
                    remoteIP[i-(lastpointer)] = 0x00;
                    lastpointer = i+1;
                    break;
                }else{
                    remoteIP[i-(lastpointer)] = buffAT_cpy[i];
                }
            }

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
            numRemotePort = atoi(remotePORT);
            
            if(len > 0 && data){
                memcpy(data, buffAT_cpy + lastpointer, len); //memcpy with offset in source
            }

            if (addr && strlen(remoteIP) > 0){
                unsigned char ip[MOBILE_INET_PTON_MAXLEN];
                int rc = mobile_inet_pton(MOBILE_INET_PTON_ANY, remoteIP, ip);

                switch (rc) {
                case MOBILE_INET_PTON_IPV4:
                    addr4->type = MOBILE_ADDRTYPE_IPV4;
                    addr4->port = numRemotePort;
                    memcpy(addr4->host, ip, sizeof(addr4->host));
                    break;
                case MOBILE_INET_PTON_IPV6:
                    addr6->type = MOBILE_ADDRTYPE_IPV6;
                    addr6->port = numRemotePort;
                    memcpy(addr6->host, ip, sizeof(addr6->host));
                    break;
                default:
                    break;
                }
            }
        }
    }

    FlushATBuff();
    if (!data){
        if(!ESP_GetSockStatus(UART_ID,conn,user)){
            return -2;
        }else{
            return 0;
        }
    }
    
    if(mobile->esp_sockets[conn].host_type == 1){         
        if(!ESP_GetSockStatus(UART_ID,conn,user)){
            if(ipdVal[conn] == 0){
                if(ESP_ReadBuffSize(UART_ID,conn) == 0) return -2;
            }else{
                if (ipdVal[conn] < 0) return -1;
            }
        }
        
        len = ESP_ReqDataBuff(UART_ID,conn,size);

        if(len > 0 ){
            memcpy(data,buffRecData + buffRecData_pointer,len);
            buffRecData_pointer = buffRecData_pointer + len;
            if(buffRecData_pointer >= ipdVal[conn]){
                buffRecData_pointer = 0;
                ipdVal[conn] = 0;
            } 
        }else{
            return len;
        }
    }
    return len;
}

static void impl_update_number(void *user, enum mobile_number type, const char *number){
    struct mobile_user *mobile = (struct mobile_user *)user;

    char *dest = NULL;
    switch (type) {
        case MOBILE_NUMBER_USER: dest = mobile->number_user; break;
        case MOBILE_NUMBER_PEER: dest = mobile->number_peer; break;
        default: assert(false); return;
    }

    if (number) {
        strncpy(dest, number, MOBILE_MAX_NUMBER_SIZE);
        dest[MOBILE_MAX_NUMBER_SIZE] = '\0';
    } else {
        dest[0] = '\0';
    }
}


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
    #ifdef CONFIG_MODE
        char newSSID[28] = "WiFi_SSID";
        char newPASS[28] = "P@$$w0rd";

        char MAGB_DNS1[60] = "0.0.0.0";
        char MAGB_DNS2[60] = "0.0.0.0";
        unsigned MAGB_DNSPORT = 53;

        char RELAY_SERVER[60] = "0.0.0.0";
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