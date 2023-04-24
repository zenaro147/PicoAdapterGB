#include "common.h"

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
    unsigned long timeAgora = 0;
    timeAgora = time_us_64(); 
    unsigned long timeCheck = 0;
    timeCheck = timeAgora - millis_latch;

    // unsigned long teste = 0;
    // teste = timeAgora - millis_latch;
    // int validate;
    // validate = 0;
    
    // printf("time: %i\n",timeAgora);
    // printf("millis_latch: %i\n",millis_latch);
    // printf("conta: %i\n",teste);
    // printf("MS 1: %i\n",ms);
    // printf("MS 2: %i\n",MS(ms));
    // if ((timeAgora - millis_latch) > MS(ms)){
    //     validate = 1;
    // }
    // printf("validate: %d\n",validate);
    if (timeCheck >= MS(ms)){
        printf("timeout1\n");
        return true;
    }
    // printf("timeout2\n");
    return false;
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
            busy_wait_us(MS(300));

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
                printf("Copiou %d\n",len);
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
            printf("retorna -2\n");
            return -2;
        }else{
            printf("retorna 0\n");
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
    printf("retorna %d\n",len);
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

