#ifndef ESP_AT_H
#define ESP_AT_H

#include "hardware/uart.h"
#include "hardware/irq.h"

#define BUFF_AT_SIZE 2048
char buffATrx[BUFF_AT_SIZE] = {};
int buffATrx_pointer = 0;
bool use_uart0 = true;

// RX interrupt handler
void on_uart_rx(){
    while (uart_is_readable(use_uart0 ? uart0 : uart1)) {
        char ch = uart_getc(use_uart0 ? uart0 : uart1);
        if(ch == '\n' || (uint8_t)ch == 255){
            continue;
        }else{
            if(ch == '\r'){
                buffATrx[buffATrx_pointer++] = '|';
            }else{
                buffATrx[buffATrx_pointer++] = ch;
            }
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

void FlushCmdBuff(){
    buffATrx_pointer=0;
    memset(buffATrx,'\0',sizeof(buffATrx));
}

void SendESPcmd(uart_inst_t *uart, const char *command){
    char cmd[300] = {}; //should handle the GET requests
    memset(cmd,'\0',sizeof(cmd));
    sprintf(cmd,"%s\r\n",command);
    uart_puts(uart, cmd);
}

void ReadESPcmd(int timeout){
    RunTimeout(timeout); // Just run a little timeout to give some time to fill the Buffer

    printf("test"); // Data seems ok... need to parse this now
    // strstr
    // strtok

    //char string[50] = "Hello world";
    //// Extract the first token
    //char * token = strtok(string, " ");
    //printf( " %s\n", token ); //printing the token
    //
    //char * token2 = strtok(NULL, " ");
    //printf( " %s\n", token2 ); //printing the token
}

void ConnectESPWiFi(uart_inst_t * uart, char * SSID_WiFi, char * Pass_WiFi){
    // Set WiFi Mode to Station 
    SendESPcmd(UART_ID,"AT+CWMODE=1");
    ReadESPcmd(SEC(2));
    FlushCmdBuff();

    // Prepare the command to send
    char espComm[100] = {};
    memset(espComm,'\0',sizeof(espComm));
    sprintf(espComm,"AT+CWJAP=\"%s\",\"%s\"",SSID_WiFi,Pass_WiFi);
    SendESPcmd(uart, espComm);
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