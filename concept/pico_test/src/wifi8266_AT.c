//GPIO23 - LED RGB WS2812 (NeoPixelConnect)
//GPIO25 - LED_BUILTIN

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#define WiFiSSID "WiFi_Network"
#define WiFiPSK "P@$$w0rd"

#define BUFFER_SIZE 2048 //1024 -- max receive from esp 2048 >> 255

#define LED_PIN         25
#define LED_SET(A)      (gpio_put(LED_PIN, (A)))
#define LED_ON          LED_SET(true)
#define LED_OFF         LED_SET(false)
#define LED_TOGGLE      (gpio_put(LED_PIN, !gpio_get(LED_PIN)))

volatile uint64_t time_us_now = 0;
volatile uint64_t last_readable = 0;

#define SEC(A)                  ((A) * 1000 * 1000)

#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4 //4
#define UART_RX_PIN 5 //5
//#define PIN_SPI_SIN     16 //0 -- just set to 4 during the debug process
//#define PIN_SPI_SCK     18 //2
//#define PIN_SPI_SOUT    19 //3 

char strcmd[BUFFER_SIZE];
int strpointer = 0;

// RX interrupt handler
void on_uart_rx(){
    while (uart_is_readable(UART_ID)) {
        char ch = uart_getc(UART_ID);
        if(ch == '\n' || (uint8_t)ch == 255){
            continue;
        }else{
            if(ch == '\r'){
                strcmd[strpointer++] = '|';
            }else{
                strcmd[strpointer++] = ch;
            }
        }
    }
}

void FlushCmdBuff(){
    memset(strcmd,'\0',sizeof(strcmd));
}

void SendESPcmd(uart_inst_t *uart, const char *command){
    char cmd[300] = {}; //should handle the GET requests
    sprintf(cmd,"%s\r\n",command);
    uart_puts(uart, cmd);
}

void ReadESPcmd(int timeout){
    //Just run a little timeout to give some time to fill the Buffer
    time_us_now = time_us_64();
    last_readable = time_us_now;
    while ((time_us_now - last_readable) < timeout){
        time_us_now = time_us_64();
    }
    printf("%s",strcmd);
    // strstr
    // strtok
}




void main() {
    sleep_ms(2000); 
    // Enable UART so we can print
    stdio_init_all();

   //char string[50] = "Hello world";
   //// Extract the first token
   //char * token = strtok(string, " ");
   //printf( " %s\n", token ); //printing the token
   //
   //char * token2 = strtok(NULL, " ");
   //printf( " %s\n", token2 ); //printing the token

    // For toggle_led
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    LED_ON;

    // Set up our UART with the required speed.
    int baud = uart_init(UART_ID, BAUD_RATE);
    bool test = uart_is_enabled(UART_ID);

    uart_set_fifo_enabled(UART_ID,false);
    uart_set_format(UART_ID,8,1,0);
    uart_set_hw_flow(UART_ID,false,false); 

    // Set the TX and RX pins by using the function select on the GPIO
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    printf("\n");
    //sleep_ms(5000); //Only use without PicoProble
    printf("Init Serial\n");
    printf("Baudrate: %i \n",baud);
    printf("Enabled: %i \n",(uint8_t)test); 


    // Set up a RX interrupt
    // Select correct interrupt for the UART we are using
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    // Set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART_ID, true, false);


 //
 // CONFIGURE THE ESP
 //   
    //test connection (return echo)
    SendESPcmd(UART_ID,"AT");
    ReadESPcmd(SEC(2));
    FlushCmdBuff();

    // Disable echo
    strpointer = 0;
    SendESPcmd(UART_ID,"ATE0");
    ReadESPcmd(SEC(2));
    FlushCmdBuff();

    // Set WiFi Mode to Station 
    strpointer = 0;
    SendESPcmd(UART_ID,"AT+CWMODE=1");
    ReadESPcmd(SEC(2));
    FlushCmdBuff();

    // Set to Passive Mode to receive TCP info
    // AT+CIPRECVDATA=<size> | read the X amount of data from esp buffer
    // AT+CIPRECVLEN? | return the remaining  buffer size like this +CIPRECVLEN:636,0,0,0,0)
    // Also can read the actual size reading the +IPD value from "AT+CIPSEND" output: \r\n\r\nRecv 60 bytes\r\n\r\nSEND OK\r\n\r\n+IPD,636\r\nCLOSED\r\n
    strpointer = 0;
    SendESPcmd(UART_ID, "AT+CIPRECVMODE=1");
    ReadESPcmd(SEC(2));

    strpointer = 0; 
    char espComm[100] = {};
    sprintf(espComm,"AT+CWJAP=\"%s\",\"%s\"",WiFiSSID,WiFiPSK);
    SendESPcmd(UART_ID, espComm);
    ReadESPcmd(SEC(10)); // WIFI DISCONNECT
    ReadESPcmd(SEC(10)); // WIFI CONNECTED
    ReadESPcmd(SEC(10)); // WIFI GOT IP
    ReadESPcmd(SEC(10)); // OK
    FlushCmdBuff();
//
// END CONFIGURE
//
   
    printf("\nDone\n");
    LED_OFF;

    while (true) {
        sleep_ms(1000);
    }
}