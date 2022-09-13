#ifndef ESP_AT_H
#define ESP_AT_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hardware/uart.h"
#include "hardware/irq.h"

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
bool isConnectedHost = false;

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

// Clean the RX buffer and ignore any data on it
void FlushATBuff(){
    buffATrx_pointer=0;
    memset(buffATrx,'\0',sizeof(buffATrx));
}

// Send a AT command to the ESP (don't read anything back, for this use the ReadESPcmd function)
void SendESPcmd(uart_inst_t *uart, const char *command){
    char cmd[300] = {}; //should handle the GET requests
    memset(cmd,'\0',sizeof(cmd));
    sprintf(cmd,"%s\r\n",command);
    uart_puts(uart, cmd);
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

bool OpenESPSockConn(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port){
    if(ipdVal[connID] != 0){
        printf("ESP-01 Start Host Connection: You can't request more data now.\n");
        return false;
    }
    
    char cmdSckt[100];
    sprintf(cmdSckt,"AT+CIPSTART=%i,\"%s\",\"%s\",%i", connID, sock_type, conn_host, conn_port);
    SendESPcmd(uart, cmdSckt);
    char * resp = ReadESPcmd(10*1000*1000); //10 seconds
    if(strstr(resp, "CONNECT") != NULL){
        resp = ReadESPcmd(5*1000*1000); //5 seconds
        if(strcmp(resp, "OK") == 0){
            printf("ESP-01 Start Host Connection: OK\n");
        }else{
            if(strcmp(resp, "ALREADY CONNECTED") == 0) {
                printf("ESP-01 Start Host Connection: ALREADY CONNECTED\n");
            }else{
                printf("ESP-01 Start Host Connection: ERROR\n");
                return false;
            } 
        }
    }else{
        if(strcmp(resp, "ALREADY CONNECTED") == 0) {            
            printf("ESP-01 Start Host Connection: ALREADY CONNECTED\n");
        }else{
            printf("ESP-01 Start Host Connection: ERROR\n");
            return false;
        }        
    }
    return true;
}

// Establish a connection to the Host (Mobile Adapter GB server) and send the GET request to some URL
bool SendESPGetReq(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, char * urlToRequest){
    // Connect the ESP to the Host
    char cmdGetReq[100] = {};
    // Prepare the GET command to send
    memset(cmdGetReq, '\0', sizeof(cmdGetReq));
    sprintf(cmdGetReq,"GET %s HTTP/1.0\r\nHost: %s\r\n", urlToRequest, conn_host);
    int cmdSize = strlen(cmdGetReq) + 2;

    // Check if the GET command have less than 2048 bytes to send. This is the ESP limit
    if(cmdSize > 2048){
        printf("ESP-01 Sending Request: ERROR - The request limit is 2048 bytes. Your request have: %i bytes\n", cmdSize);
    }else{
        // Send the ammount of data we will send to ESP (the GET command size)
        char cmdSend[20] = {};
        sprintf(cmdSend,"AT+CIPSEND=%i,%i", connID, cmdSize);
        SendESPcmd(uart, cmdSend);
        char * resp = ReadESPcmd(2*1000*1000);
        if(strcmp(resp, "OK") == 0){
            printf("ESP-01 Sending Request: OK\nSending Request...\n");
            FlushATBuff(); // Clean the '>' signal to receive the command
            SendESPcmd(uart, cmdGetReq); // Finally send the GET request!It have one more \r\n at the end, but the SendESPcmd already do this.
            //Possible returns: ERROR, SEND OK, SEND FAIL
            resp = ReadESPcmd(10*1000*1000); //10 sec, "Received Bytes message" (unused data, but feeds the buffer if necessary)
            resp = ReadESPcmd(1*1000*1000);
            if(strcmp(resp, "SEND OK") == 0){
                printf("ESP-01 Sending Request: SEND OK\n");
                resp = ReadESPcmd(1*1000*1000);
                if(strstr(resp, "+IPD") != NULL){
                    char numipd[4] = {};
                    for(int i = 7; i < strlen(resp); i++){
                        numipd[i-7] = resp[i]; 
                    }
                    //Set the IPD value into a variable to control the data to send
                    ipdVal[connID] = atoi(numipd);
                    printf("ESP-01 Bytes Received: %i\n", ipdVal[connID]);
                    resp = ReadESPcmd(1*1000*1000);
                    if(strcmp(resp, "CLOSED") == 0){
                        isConnectedHost=false;
                    }
                    return true;
                }else{
                    printf("ESP-01 Sending Request: ERROR\n");
                }
            }else{
                printf("ESP-01 Sending Request: ERROR\n");
            }
        }else{
            printf("ESP-01 Sending Request: ERROR\n");
        }
    }
    return false;
}

// Retrieve data from the ESP buffer (max Data Size = 2048)
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
            SendESPcmd(uart,cmdRead); //Must igonre the OK at the end, and the "+CIPRECVDATA,<size>:" at the beginning
            Delay_Timer(5*1000*1000); //5 sec. Give time to feed the buffer    
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

//Read the remaining data inside ESP buffer (must be used like this: ipdVal = ReadESPGetReqBuffSize(UART_ID))
int ReadESPGetReqBuffSize(uart_inst_t * uart, uint8_t connIDReq){
    uint8_t connID = 0;
    SendESPcmd(uart,"AT+CIPRECVLEN?");
    char * resp = ReadESPcmd(2*1000*1000);
    if(strstr(resp, "+CIPRECVLEN:") != NULL){
        // Extract the first token
        char * token = strtok(resp, ":");
        // loop through the string to extract all other tokens
        while(token != NULL) {
            if(strcmp(token, "+CIPRECVLEN") != 0){
                ipdVal[connID++] = atoi(token);
            }
            token = strtok(NULL, ",");
        }
        FlushATBuff();
        return ipdVal[connIDReq]; 
    }else{        
        printf("ESP-01 Host Status: ERROR\n");
    }
    return 0;   
}

// Return the status of the Host connection using TCP or UDP (must be used like this: isConnectedHost = GetESPHostConn(UART_ID))
bool GetESPHostConn(uart_inst_t * uart){
    SendESPcmd(uart,"AT+CIPSTATUS");
    char * resp = ReadESPcmd(2*1000*1000);
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

void CloseESPGetReq(uart_inst_t * uart, uint8_t connID){
    char cmd[15];
    sprintf(cmd,"AT+CIPCLOSE=%i",connID);
    SendESPcmd(uart,cmd);
    Delay_Timer(2*1000*1000); // 2 seconds
    FlushATBuff(); // Clean any output. We don't need it.
    GetESPHostConn(uart);
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
bool ConnectESPWiFi(uart_inst_t * uart, char * SSID_WiFi, char * Pass_WiFi, int timeout){
    
    //AT+CIPSTO=0 -- set Server timeout (0~7200)
    
    //test connection - Check 2 times in case the ESP return some echo 
    FlushATBuff(); // Reset RX Buffer
    SendESPcmd(uart,"AT");
    char * resp = ReadESPcmd(1*1000*1000);
    if(strcmp(resp, "OK") == 0){
        printf("ESP-01 Connectivity: OK\n");
    }else{        
        printf("ESP-01 Connectivity: ERROR || Retrying...\n");
        resp = ReadESPcmd(1*1000*1000);
        if(strcmp(resp, "OK") == 0){
            printf("ESP-01 Connectivity: OK\n");
        }else{        
            printf("ESP-01 Connectivity: ERROR\n");
        }
    }

    // Disable echo - Check 2 times in case the ESP return some echo 
    SendESPcmd(uart,"ATE0");
    resp = ReadESPcmd(1*1000*1000);
    if(strcmp(resp, "OK") == 0){
        printf("ESP-01 Disable Echo: OK\n");
    }else{        
        printf("ESP-01 Disable Echo: ERROR || Retrying...\n");
        resp = ReadESPcmd(1*1000*1000);
        if(strcmp(resp, "OK") == 0){
            printf("ESP-01 Disable Echo: OK\n");
        }else{        
            printf("ESP-01 Disable Echo: ERROR\n");
        }
    }

    // Set to Passive Mode to receive TCP info
    // AT+CIPRECVDATA=<size> | read the X amount of data from esp buffer
    // AT+CIPRECVLEN? | return the remaining  buffer size like this +CIPRECVLEN:636,0,0,0,0)
    SendESPcmd(uart, "AT+CIPRECVMODE=1");
    resp = ReadESPcmd(1*1000*1000); // 2 second
    if(strcmp(resp, "OK") == 0){
        printf("ESP-01 Passive Mode: OK\n");
    }else{        
        printf("ESP-01 Passive Mode: ERROR\n");
    }

    SendESPcmd(uart, "AT+CIPMUX=1");
    resp = ReadESPcmd(1*1000*1000); // 2 second
    if(strcmp(resp, "OK") == 0){
        printf("ESP-01 Multi Connections: OK\n");
    }else{        
        printf("ESP-01 Multi Connections: ERROR\n");
    }

    // Set WiFi Mode to Station mode Only
    SendESPcmd(uart,"AT+CWMODE=1");
    resp = ReadESPcmd(1*1000*1000);
    if(strcmp(resp, "OK") == 0){
        printf("ESP-01 Station Mode: OK\n");
        // Prepare the command to send
        char espComm[100] = {};
        memset(espComm,'\0',sizeof(espComm));
        sprintf(espComm,"AT+CWJAP=\"%s\",\"%s\"",SSID_WiFi,Pass_WiFi);
        SendESPcmd(uart, espComm);

        resp = ReadESPcmd(timeout);
        while (true){
            if(strcmp(resp, "WIFI DISCONNECT") == 0){
                printf("ESP-01 Connecting Wifi: DISCONNECTED\n");
            }else{
                if(strcmp(resp, "WIFI CONNECTED") == 0){
                    printf("ESP-01 Connecting Wifi: CONNECTED\n");
                }else{
                    if(strcmp(resp, "WIFI GOT IP") == 0){
                        printf("ESP-01 Connecting Wifi: GOT IP\n");
                    }else{
                        if(strcmp(resp, "OK") == 0){
                            printf("ESP-01 Connecting Wifi: OK\n");
                            return true;
                        }else{
                            printf("ESP-01 Connecting Wifi: ERROR\n");
                            return false;
                        }                        
                    }
                }
            }
            resp = ReadESPcmd(1*1000*1000); // 2 seconds
        }
    }else{        
        printf("ESP-01 Station Mode: ERROR\n");
        return false;
    }
}
#endif