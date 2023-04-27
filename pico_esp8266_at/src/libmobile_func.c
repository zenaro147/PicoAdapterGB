#include "libmobile_func.h"

#include <string.h>

#include "global.h"
#include "flash_eeprom.h"
#include "esp_at.h"

#include <mobile_inet.h>


/////////////////////////
// Libmobile Functions
/////////////////////////

bool socket_impl_open(struct esp_sock_config *state, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    // Returns: true if socket was created successfully, false on error
    FlushATBuff();

    if(state->host_id != -1 && !state->sock_status){
        state->host_id = -1;
        return false;
    }

    switch (addrtype) {
        case MOBILE_ADDRTYPE_IPV4:
            state->host_iptype = ADDR_IPV4;
            break;
        case MOBILE_ADDRTYPE_IPV6:
            state->host_iptype = ADDR_IPV6;
            break;
        default: 
            return false;
    }

    switch (socktype) {
        case MOBILE_SOCKTYPE_TCP:
            state->host_type = TYPE_TCP;
            break;
        case MOBILE_SOCKTYPE_UDP: 
            state->host_type = TYPE_UDP;
            break;
        default: 
            return false;
    }
    
    state->host_id = conn;
    state->local_port = bindport;

    if(state->host_type == TYPE_UDP && !state->sock_status){
        bool checkUDPsock = false;
        checkUDPsock = ESP_OpenSockConn(UART_ID, conn, state->host_iptype == ADDR_IPV4 ? "UDP" : "UDPv6", state->host_iptype == ADDR_IPV4 ? "127.0.0.1" : "0:0:0:0:0:0:0:1", 1, state->local_port, 2);
        if(!checkUDPsock){
            state->host_id = -1;
            state->local_port = -1;
            state->sock_status = false;
        }else{
            state->sock_status = checkUDPsock;
        }
        return checkUDPsock;
    }
    return true;
}

void socket_impl_close(struct esp_sock_config *state){
    if(state->sock_status){
        ESP_CloseSockConn(UART_ID, state->host_id);
    }else if(state->isServerOpened){
        ESP_CloseServer(UART_ID);
    }
    state->host_id = -1;
    state->host_iptype = ADDR_NONE;
    state->host_type = TYPE_NONE;
    memset(state->host_addr,0x00,sizeof(state->host_addr));
    state->local_port = -1;
    state->remote_port = -1;
    state->sock_status = false;
    state->isServerOpened = false;
    state->ipdVal = 0;
}

int socket_impl_connect(struct esp_sock_config *state, const struct mobile_addr *addr){
    // Returns: 1 on success, 0 if connect is in progress, -1 on error
    if(state->host_id == -1 && !state->sock_status){
        return -1;
    }
    FlushATBuff();

    char srv_ip[46];
    memset(srv_ip,0x00,sizeof(srv_ip));
    int srv_port=0;
    char sock_type[5];

    if (addr->type == MOBILE_ADDRTYPE_IPV4) {
        const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
        srv_port=addr4->port;
        if (state->host_type == TYPE_TCP) {
            sprintf(sock_type,"TCP");
        }else if(state->host_type == TYPE_UDP){
            sprintf(sock_type,"UDP");
        }else{
            return -1;
        }
    } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
        const struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
        //Need to parse IPV6
        //sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
        srv_port=addr6->port;
        if (state->host_type == TYPE_TCP) {
            sprintf(sock_type,"TCPv6");
        }else if(state->host_type == TYPE_UDP){
            sprintf(sock_type,"UDPv6");
        }else{
            return -1;
        } 
        return -1;
    } else {
        return -1;
    }

    if(state->ipdVal != 0){
        printf("You can't open a connection now.\n");
        return -1;
    }
    
    if(state->host_type == TYPE_TCP){
        state->sock_status = ESP_OpenSockConn(UART_ID, state->host_id, sock_type, srv_ip, srv_port, state->local_port, 0);
    }else if(state->host_type == TYPE_UDP){
        state->sock_status = ESP_OpenSockConn(UART_ID, state->host_id, sock_type, state->host_iptype == ADDR_IPV4 ? "127.0.0.1" : "0:0:0:0:0:0:0:1", 1, state->local_port, 2);
    }
        
    if(state->sock_status) return 1;

    return -1;
}

bool socket_impl_listen(struct esp_sock_config *state){
    // Returns: true if a connection was accepted,
    //          false if there's no incoming connections    
    bool sockP2Pcheck = false;
    if(!state->isServerOpened && state->host_type == TYPE_TCP){
        sockP2Pcheck = ESP_OpenServer(UART_ID,state->host_id,state->local_port);
        state->isServerOpened = sockP2Pcheck;
    }
    return sockP2Pcheck;
}

bool socket_impl_accept(struct esp_sock_config *state){
    // Returns: true if a connection was accepted,
    //          false if there's no incoming connections
    bool sockP2Pconn = false;
    if(state->isServerOpened){
        sockP2Pconn = ESP_CheckIncommingConn(UART_ID,state->host_id);
        if(sockP2Pconn){
            state->sock_status = true;
        }
    }    
    return sockP2Pconn;
}
 
int socket_impl_send(struct esp_sock_config *state, const void *data, const unsigned size, const struct mobile_addr *addr){   
    // Returns: non-negative amount of data sent on success, -1 on error
    if(state->host_id == -1 && !state->sock_status){
        return -1;
    }
    
    char srv_ip[46];
    memset(srv_ip,0x00,sizeof(srv_ip));
    int srv_port=0;
    char sock_type[5];
    uint8_t sendDataStatus;

    FlushATBuff();
    if(state->host_type == TYPE_UDP){
        if (addr->type == MOBILE_ADDRTYPE_IPV4) {
        const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
        srv_port=addr4->port;
        } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
            const struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
            //Need to parse IPV6
            srv_port=addr6->port;
        }
        sendDataStatus = ESP_SendData(UART_ID, state->host_id, "UDP" , srv_ip, srv_port, data, size);
    }else if(state->host_type == TYPE_TCP){
        sendDataStatus = ESP_SendData(UART_ID, state->host_id, "TCP" , "0.0.0.0", 0, data, size);
        char checkClose[12];
        sprintf(checkClose,"%i,CLOSED\r\n",state->host_id);
        if (strstr(buffATrx,checkClose) != NULL){
            state->host_id = -1;
            state->host_iptype = ADDR_NONE;
            state->host_type = TYPE_NONE;
            memset(state->host_addr,0x00,sizeof(state->host_addr));
            state->local_port = -1;
            state->remote_port = -1;
            state->sock_status = false;
            state->isServerOpened = false;
            state->ipdVal = 0;
            FlushATBuff();
            printf("Socket Closed by remote host.\n");
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

int socket_impl_recv(struct esp_sock_config *state, void *data, unsigned size, struct mobile_addr *addr){
    // Returns: amount of data received on success,
    //          -1 on error,
    //          -2 on remote disconnect

    struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
    struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;

    int len = -1;
    int numRemotePort=-1;
    char remoteIP[50] = {0};

    //Checking if there is any UDP data in the buffer
    if(state->host_type == TYPE_UDP){
        char cmdCheck[10];
        sprintf(cmdCheck,"+IPD,%i,",state->host_id);
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
        if(!ESP_GetSockStatus(UART_ID,state->host_id)){
            return -2;
        }else{
            return 0;
        }
    }
    
    if(state->host_type == TYPE_TCP){
        if(!ESP_GetSockStatus(UART_ID,state->host_id)){
            if(state->ipdVal == 0){
                if(ESP_ReadBuffSize(UART_ID,state->host_id) == 0) return -2;
            }else{
                if (state->ipdVal < 0) return -1;
            }
        }
        
        int dataToRead = 0;
        if(state->ipdVal <= 0){
            state->ipdVal = ESP_ReadBuffSize(UART_ID,state->host_id);
        }
        if(state->ipdVal == 0){
            printf("You don't have data to read.\n");
            
            return 0;
        }
        if(state->ipdVal > MOBILE_MAX_TRANSFER_SIZE){
            printf("The maximum data to read is %i.\n",MOBILE_MAX_TRANSFER_SIZE);
            dataToRead = MOBILE_MAX_TRANSFER_SIZE;
        }else{
            dataToRead = state->ipdVal;
        }
        if(state->ipdVal > size){
            dataToRead = size;
        }
        printf("Requesting %i.\n",dataToRead);
        len = ESP_ReqDataBuff(UART_ID,state->host_id,dataToRead);

        if (len > 0){
            memcpy(data,buffRecData,len);
            state->ipdVal = state->ipdVal-len;
        }
    }
    return len;
}