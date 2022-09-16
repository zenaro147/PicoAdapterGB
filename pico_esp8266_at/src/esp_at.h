#ifndef ESP_AT_H
#define ESP_AT_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "common.h"

#define buffDelimiter '|'
#define BUFF_AT_SIZE 2048 //2048 is the maximun you can receive from esp01
char buffATrx[BUFF_AT_SIZE] = {};
int buffATrx_pointer = 0;
char buffGETReq[BUFF_AT_SIZE] = {};
int buffGETReq_pointer = 0;
bool use_uart0 = true;

bool isConnectedWiFi = false;
bool ishttpRequest = false;
int ipdVal[5] = {0,0,0,0,0};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// RX interrupt handler
void on_uart_rx(){
    while (uart_is_readable(use_uart0 ? uart0 : uart1)) {
        char ch = uart_getc(use_uart0 ? uart0 : uart1);
        if(!ishttpRequest){
            if(ch == '\n' || (buffATrx[buffATrx_pointer-1] == buffDelimiter && ch == '\r')){
                continue;
            }else{
                if(ch == '\r'){
                    if (buffATrx_pointer > 0){
                        buffATrx[buffATrx_pointer++] = buffDelimiter;
                    }
                }else{
                    buffATrx[buffATrx_pointer++] = ch;
                }
            }
        }else{
            buffATrx[buffATrx_pointer++] = ch;
        }
    }
}

// Clean the RX buffer and ignore any data on it
void FlushATBuff(){
    buffATrx_pointer=0;
    memset(buffATrx,'\0',sizeof(buffATrx));
}

// Find a string into the RX buffer
bool ESP_SerialFind(char * buf, char * target, int ms_timeout, bool cleanBuff){
    volatile uint64_t timenow = time_us_64();
    volatile uint64_t last_read = timenow;

    while ((timenow - last_read) < ms_timeout){
        timenow = time_us_64();
        if(strstr(buf, target) != NULL){
            if(cleanBuff) FlushATBuff();
            return true;
        }
    }
    if(cleanBuff) FlushATBuff();
    return false;
}

// Run a little timeout instead sleep (to prevent any block to the RX interrupt)
void Delay_Timer(int timeout){
    volatile uint64_t timenow = 0;
    volatile uint64_t last_read = 0;
    timenow = time_us_64();
    last_read = timenow;
    while ((timenow - last_read) < timeout){
        timenow = time_us_64();
    }
}

// *********************Send a AT command to the ESP (don't read anything back, for this use the ReadESPcmd function)
void ESP_SendCmd(uart_inst_t *uart, const uint8_t *command){
    char cmd[300] = {}; //should handle the GET requests
    memset(cmd,'\0',sizeof(cmd));
    sprintf(cmd,"%s\r\n",command);
    for (int i = 0; i< sizeof(cmd); i++){
        uart_putc(uart, cmd[i]);
    }
    
}

// Read the internal RX buffer with the received data from ESP
char * ReadESPcmd(int timeout){
    char buff_answer[BUFF_AT_SIZE/4] = {};
    Delay_Timer(timeout); // Just run a little timeout to give some time to fill the Buffer
    if(strlen(buffATrx) > 0 && buffATrx_pointer > 0){
        char buffATrx_cpy[sizeof(buffATrx)] = {};
        
        memset(buff_answer,'\0',sizeof(buff_answer));
        memcpy(buffATrx_cpy, buffATrx, sizeof(buffATrx));
        
        //Return the first anwser in buffer
        int i = 0;
        while (buffATrx[i] != buffDelimiter){
            buff_answer[i] = buffATrx[i];
            i++;
        }
        i++; //Skip the pipe delimiter to the next character

        if(buffATrx[i] != '\0'){
            //Remove the answer from RX buffer and let only the unread messages
            memset(buffATrx,'\0',sizeof(buffATrx));
            for(int x = i; x < strlen(buffATrx_cpy) ;x++){
                buffATrx[x-i] = buffATrx_cpy[x];
            }
        }else{
            //Reset the Buffer if has no messages
            FlushATBuff(); 
        }
    }

    if(strlen(buff_answer) > 0){
        char * answer_return = buff_answer;
        return answer_return;
    }else{
        return "";
    }
}

// *********************Establish a connection to the Host (Mobile Adapter GB server) and send the GET request to some URL
uint8_t ESP_SendData(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, char * urlToRequest){
    
    // Connect the ESP to the Host
    char cmdSendData[100] = {};
    // Prepare the GET command to send
    memset(cmdSendData, 0x00, sizeof(cmdSendData));
    sprintf(cmdSendData,"GET %s HTTP/1.0\r\nHost: %s\r\n", urlToRequest, conn_host);
    int cmdSize = strlen(cmdSendData) + 2;

    // Check if the GET command have less than 2048 bytes to send. This is the ESP limit
    if(cmdSize > 2048){
        printf("ESP-01 Sending Request: ERROR - The request limit is 2048 bytes. Your request have: %i bytes\n", cmdSize);
    }else{        
        // Send the ammount of data we will send to ESP (the GET command size)
        char cmdSend[100] = {};
        if(strcmp(sock_type, "UDP") == 0){
            sprintf(cmdSend,"AT+CIPSEND=%i,%i,\"%s\",%i", connID, cmdSize, conn_host, conn_port);
        }else{
            sprintf(cmdSend,"AT+CIPSEND=%i,%i", connID, cmdSize);
        }
        ESP_SendCmd(uart, cmdSend);
        if(ESP_SerialFind(buffATrx,">",MS(2),true)){         
            printf("ESP-01 Sending Data: OK\nSending Request...\n");
            ESP_SendCmd(uart, cmdSendData);
            //Possible returns: ERROR, SEND OK, SEND FAIL
            if(ESP_SerialFind(buffATrx,"SEND OK",MS(10),false)){
                printf("ESP-01 Sending Data: SEND OK\n");
                return cmdSize;
            }
        }
    }
    printf("ESP-01 Sending Request: ERROR\n");
    return -1;
}

// *********************Retrieve data from the ESP buffer (max Data Size = 2048)
void ReadESPGetReq(uart_inst_t * uart, uint8_t connID, int dataSize){
    if(ipdVal[connID] == 0){
        printf("ESP-01 Read Request: You don't have data to read.\n");
    }else{
        if(dataSize > 2048){
            printf("ESP-01 Read Request: The maximum data to read is 2048.\n");
        }else{
            if(ipdVal[connID]-dataSize < 0){
                dataSize=ipdVal[connID];        
                printf("ESP-01 Read Request: You request more data than it stored. Changing value.\n");
            }else{
                ipdVal[connID] = ipdVal[connID] - dataSize;
            }
            // Enable to raw parse the incomming data
            char cmdRead[25]={};
            buffGETReq_pointer=dataSize;
            sprintf(cmdRead,"AT+CIPRECVDATA=%i,%i",connID,dataSize);
            ishttpRequest=true;
            ESP_SendCmd(uart,cmdRead); //Must igonre the OK at the end, and the "+CIPRECVDATA,<size>:" at the beginning
            Delay_Timer(SEC(5)); //5 sec. Give time to feed the buffer    
            ishttpRequest=false;
            int cmdReadSize = strlen(cmdRead)-1;
            for(int i = cmdReadSize; i < buffATrx_pointer-6; i++){
                buffGETReq[i - cmdReadSize] = buffATrx[i];
            }
            FlushATBuff();
            printf("ESP-01 Read Request: Done.\n");
        } 
    }    
}

// *********************Read the remaining data inside ESP buffer (must be used like this: ipdVal = ESP_ReadDataBuffSize(UART_ID))
int ESP_ReadDataBuffSize(uart_inst_t * uart, uint8_t connIDReq){
    FlushATBuff();
    ESP_SendCmd(uart,"AT+CIPRECVLEN?");
    if(ESP_SerialFind(buffATrx,"+CIPRECVLEN:",MS(2000),false)){
        uint8_t connID_pointer = 0;
        char resp[50];
        memcpy(resp,buffATrx,strlen(buffATrx));
        // Extract the first token
        char * token = strtok(resp, ":");
        // loop through the string to extract all other tokens
        while(token != NULL) {
            if(strcmp(token, "+CIPRECVLEN") != 0){
                ipdVal[connID_pointer++] = atoi(token);
            }
            token = strtok(NULL, ",");
        }
        FlushATBuff();
        return ipdVal[connIDReq]; 
    }else{
       printf("ESP-01 Read Buffer Lenght: ERROR\n"); 
    }
    FlushATBuff();
    return 0;
}

//Open the desired ESP Socket
bool ESP_OpenSockConn(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, int conn_localport, uint8_t conn_mode){
    if(ipdVal[connID] != 0){
        printf("ESP-01 Start Host Connection: You can't open a connection now.\n");
        return false;
    }

    char cmdSckt[100];
    if (conn_localport != 0){
        sprintf(cmdSckt,"AT+CIPSTART=%i,\"%s\",\"%s\",%i,%i,%i", connID, sock_type, conn_host, conn_port, conn_localport, conn_mode);
    }else{
        sprintf(cmdSckt,"AT+CIPSTART=%i,\"%s\",\"%s\",%i", connID, sock_type, conn_host, conn_port);
    }
    
    ESP_SendCmd(uart, cmdSckt);
    if(ESP_SerialFind(buffATrx,"CONNECT",MS(5000),false)){
        if(ESP_SerialFind(buffATrx,"OK",MS(5000),true)){            
            printf("ESP-01 Host Connection: OK\n");
        }
    }else if(ESP_SerialFind(buffATrx,"ALREADY CONNECTED",MS(5000),false)){        
        printf("ESP-01 Start Host Connection: ALREADY CONNECTED\n");
    }else{
        printf("ESP-01 Start Host Connection: ERROR\n");
        return false;
    }

    return true;
}

// *********************Return the status of the Host connection using TCP or UDP (must be used like this: isConnectedHost = ESP_GetSockStatus(UART_ID))
bool ESP_GetSockStatus(uart_inst_t * uart, uint8_t connID){
    ESP_SendCmd(uart,"AT+CIPSTATUS");
    char * resp = ReadESPcmd(SEC(2));
    if(strstr(resp, "STATUS:") != NULL){
        char numstat[2] = {};
        for(int i = 7; i < strlen(resp); i++){
            numstat[i-7] = resp[i]; 
        }
        FlushATBuff();
        uint8_t numstat_val = 0;
        numstat_val = atoi(numstat);
        if(numstat_val == 3){ // The ESP8266 Station has created a TCP or UDP transmission. 
            printf("ESP-01 Host Status: TCP/UDP still connected.\n");
            return true;
        }else{
            printf("ESP-01 Host Status: All TCP/UDP disconnected.\n");
        }
    }else{        
        printf("ESP-01 Host Status: ERROR\n");
    }
    return false;    
}

// *********************Close the desired ESP Socket
void ESP_CloseSockConn(uart_inst_t * uart, uint8_t connID){
    char cmd[15];
    sprintf(cmd,"AT+CIPCLOSE=%i",connID);
    ESP_SendCmd(uart,cmd);
    if(ESP_SerialFind(buffATrx,"OK",MS(2000),true)){
         printf("ESP-01 Close Socket: OK\n");
    }else{
       printf("ESP-01 Close Socket: ERROR\n"); 
    }
    //ESP_GetSockStatus(uart); //Need to recreate this function to get the status of all connections
}

// Initialize the ESP-01 UART communication
bool EspAT_Init(uart_inst_t * uart, int baudrate, int txpin, int rxpin){
    // Set up our UART with the required speed.
    int baud = uart_init(uart, baudrate);
    bool isenabled = uart_is_enabled(uart);

    use_uart0 = (uart == uart0 ? true : false);
    
    // Set the TX and RX pins by using the function select on the GPIO
    gpio_set_function(txpin, GPIO_FUNC_UART);
    gpio_set_function(rxpin, GPIO_FUNC_UART);

    uart_set_fifo_enabled(uart,false);
    uart_set_format(uart,8,1,0);
    uart_set_hw_flow(uart,false,false);     

    // Set up a RX interrupt
    // Select correct interrupt for the UART we are using
    int UART_IRQ = uart == uart0 ? UART0_IRQ : UART1_IRQ;

    // Set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Enable the UART to send interrupts - RX only
    uart_set_irq_enables(uart, true, false);

    printf("Uart Baudrate: %i \n",baud);
    printf("Uart Enabled: %i \n",(uint8_t)isenabled);
    return isenabled;
}

// Provides the necessary commands to connect the ESP to a WiFi network
bool ESP_ConnectWifi(uart_inst_t * uart, char * SSID_WiFi, char * Pass_WiFi, int timeout){
    
    //AT+CIPSTO=0 -- set Server timeout (0~7200)
    //AT+CIPDINFO=1 -- Show port and IP on IPD return
    
    //Test hardware connection
    FlushATBuff(); // Reset RX Buffer
    ESP_SendCmd(uart,"AT");
    if(ESP_SerialFind(buffATrx,"OK",MS(1000),true)){
         printf("ESP-01 Connectivity: OK\n");
    }else{
       printf("ESP-01 Connectivity: ERROR\n"); 
    }

    // Disable echo 
    ESP_SendCmd(uart,"ATE0");
    if(ESP_SerialFind(buffATrx,"OK",MS(1000),true)){
         printf("ESP-01 Disable Echo: OK\n");
    }else{
       printf("ESP-01 Disable Echo: ERROR\n"); 
    }
    
    // Set to Passive Mode to receive TCP info
    // AT+CIPRECVDATA=<size> | read the X amount of data from esp buffer
    // AT+CIPRECVLEN? | return the remaining  buffer size like this +CIPRECVLEN:636,0,0,0,0)
    ESP_SendCmd(uart, "AT+CIPRECVMODE=1");
    if(ESP_SerialFind(buffATrx,"OK",MS(1000),true)){
         printf("ESP-01 Passive Mode: OK\n");
    }else{
       printf("ESP-01 Passive Mode: ERROR\n"); 
    }

    ESP_SendCmd(uart, "AT+CIPMUX=1");
    if(ESP_SerialFind(buffATrx,"OK",MS(1000),true)){
         printf("ESP-01 Multi Connections: OK\n");
    }else{
       printf("ESP-01 Multi Connections: ERROR\n"); 
    }

    // Set WiFi Mode to Station mode Only
    ESP_SendCmd(uart,"AT+CWMODE=1");    
    if(ESP_SerialFind(buffATrx,"OK",MS(1000),true)){
         printf("ESP-01 Station Mode: OK\n");
         // Prepare the command to send
        char espComm[100] = {};
        memset(espComm,'\0',sizeof(espComm));
        sprintf(espComm,"AT+CWJAP=\"%s\",\"%s\"",SSID_WiFi,Pass_WiFi);
        ESP_SendCmd(uart, espComm);
        
        if(ESP_SerialFind(buffATrx,"OK",MS(10000),true)){
            printf("ESP-01 Connecting Wifi: OK\n");
            return true;
        }else{
            printf("ESP-01 Connecting Wifi: ERROR\n");
            return false;
        }
    }else{
       printf("ESP-01 Station Mode: ERROR\n"); 
       return false;
    }
}
#endif