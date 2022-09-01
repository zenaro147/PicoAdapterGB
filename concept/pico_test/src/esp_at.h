#ifndef ESP_AT_H
#define ESP_AT_H

#include "hardware/uart.h"
#include "hardware/irq.h"

#define buffDelimiter '|'
#define BUFF_AT_SIZE 512 //2048 is the maximun you can receive from esp01
char buffATrx[BUFF_AT_SIZE] = {};
int buffATrx_pointer = 0;
bool use_uart0 = true;
bool ishttpRequest = false;

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

void RunTimeout(int timeout){
    volatile uint64_t timenow = 0;
    volatile uint64_t last_read = 0;
    timenow = time_us_64();
    last_read = timenow;
    while ((timenow - last_read) < timeout){
        timenow = time_us_64();
    }
}

void FlushATBuff(){
    buffATrx_pointer=0;
    memset(buffATrx,'\0',sizeof(buffATrx));
}

void SendESPcmd(uart_inst_t *uart, const char *command){
    char cmd[300] = {}; //should handle the GET requests
    memset(cmd,'\0',sizeof(cmd));
    sprintf(cmd,"%s\r\n",command);
    uart_puts(uart, cmd);
}

char * ReadESPcmd(int timeout){
    char buff_answer[BUFF_AT_SIZE] = {};
    RunTimeout(timeout); // Just run a little timeout to give some time to fill the Buffer
    if(strlen(buffATrx) > 0 && buffATrx_pointer > 0){
        char buffATrx_cpy[BUFF_AT_SIZE] = {};
        
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
            memset(buffATrx,'\0',sizeof(buffATrx));
            for(int x = i; x < strlen(buffATrx_cpy) ;x++){
                buffATrx[x-i] = buffATrx_cpy[x];
            }
        }else{
            FlushATBuff(); //Reset the Buffer if has no messages
        }
    }

    if(strlen(buff_answer) > 0){
        char * answer_return = buff_answer;
        return answer_return;
    }else{
        return "";
    }

    
    
}

bool ConnectESPWiFi(uart_inst_t * uart, char * SSID_WiFi, char * Pass_WiFi){
    // Set WiFi Mode to Station 
    SendESPcmd(uart,"AT+CWMODE=1");
    char * resp = ReadESPcmd(2*1000*1000); // 2 seconds
    if(strcmp(resp, "OK") == 0){
        printf("ESP-01 Station Mode: OK\n");
        // Prepare the command to send
        char espComm[100] = {};
        memset(espComm,'\0',sizeof(espComm));
        sprintf(espComm,"AT+CWJAP=\"%s\",\"%s\"",SSID_WiFi,Pass_WiFi);
        SendESPcmd(uart, espComm);
        return true;
    }else{        
        printf("ESP-01 Station Mode: ERROR\n");
        return false;
    }
}

void EspAT_Init(uart_inst_t * uart, int baudrate, int txpin, int rxpin){
    // Set up our UART with the required speed.
    int baud = uart_init(uart, baudrate);
    bool isenabled = uart_is_enabled(uart);

    if(uart == uart0){
        use_uart0 = true;
    }else{
        use_uart0 = false;
    }

    uart_set_fifo_enabled(uart,false);
    uart_set_format(uart,8,1,0);
    uart_set_hw_flow(uart,false,false); 

    // Set the TX and RX pins by using the function select on the GPIO
    gpio_set_function(txpin, GPIO_FUNC_UART);
    gpio_set_function(rxpin, GPIO_FUNC_UART);

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
}
#endif