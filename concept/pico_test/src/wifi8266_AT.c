//GPIO23 - LED RGB WS2812 (NeoPixelConnect)
//GPIO25 - LED_BUILTIN

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#define WiFiSSID "WiFi_Network"
#define WiFiPSK "P@$$w0rd"

#define BUFFER_SIZE 255 //1024

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

char strcmd[100];
int strpointer = 0;

void testaESP(uart_inst_t *uart, int timeout, const char *command, bool ignoreEmpty){
    char c;
    int cmdLen = strlen(command);
    int readData = 0;
        
    uart_puts(uart, command);
    uart_puts(uart, "\r\n");

    time_us_now = time_us_64();
    last_readable = time_us_now;
    while ((time_us_now - last_readable) < timeout){
        time_us_now = time_us_64();
        if(uart_is_readable(uart)){
            c = uart_getc(uart);
            if(c != command[0] && (uint8_t)c == 255){
                cmdLen++; //Dev Note: The UART already has a "dummy data" feeded. This line just consider this to ignore the echo
            }
            
            if (c == '\r'){
                continue;
            }else{
                if(c == '\n'){
                    //if((strcmd[0] == '\0' && !ignoreEmpty) || (strcmd[0] != '\0' && strpointer != 0)){
                    if(strcmd[0] == '\0' && !ignoreEmpty){
                        printf("quit\n");
                        break;
                    }
                }else{
                    if(++readData > cmdLen){
                        strcmd[strpointer++] = c;
                    }
                }
            }
        }
    }
    //return strcmd;
}


bool BuffESPcmd(){

}

void SendESPcmd(uart_inst_t *uart, const char *command){
    char cmd[BUFFER_SIZE] = {};    
    sprintf(cmd,"%s\r\n",command);
    uart_puts(uart, cmd);
}

void ReadESPcmd(){

}







void main() {
    sleep_ms(2000); //give some time to the ESP boot

    // Enable UART so we can print
    stdio_init_all();

    // For toggle_led
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    LED_ON;
    //multicore_launch_core1(core1_context);

    int baud = 0;
    // Set up our UART with the required speed.
    baud = uart_init(UART_ID, BAUD_RATE);
    bool test = false;
    test = uart_is_enabled(UART_ID);

    uart_set_fifo_enabled(UART_ID,false);
    uart_set_format(UART_ID,8,1,0);
    uart_set_hw_flow(UART_ID,false,false); 

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    printf("\n");
    //sleep_ms(5000); //Only use without PicoProble
    printf("Init Serial\n");
    printf("Baudrate: %i \n",baud);
    printf("Enabled: %i \n",(uint8_t)test);  
    
    //Disable echo
    testaESP(UART_ID, SEC(5), "AT", true);
    printf("%s\n",strcmd); 
    //�AT\r\n\r\nOK\r\n
    //OK
    
    //strpointer = 0;
    //testaESP(UART_ID, SEC(5), "ATE0", true);
    //printf("%s\n",strcmd); 
    //\n\r\nOK\r\n
    //OK

    strpointer = 0;
    testaESP(UART_ID, SEC(5), "AT+CWMODE=1", true);
    printf("%s\n",strcmd); 
    //AT+CWMODE=1\r\n\r\nOK\r\n
    //\n\r\nOK\r\n
    //OK

    strpointer = 0; 
    char espComm[100] = {};
    sprintf(espComm,"AT+CWJAP=\"%s\",\"%s\"",WiFiSSID,WiFiPSK);
    testaESP(UART_ID, SEC(10), espComm, false);
    printf("%s\n",strcmd); 
    //AT+CWJAP="WiFi_Network","P@$$w0rd"\r\nWIFI DISCONNECT\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n
    //WIFI DISCONNECT\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n
    //WIFI DISCONNECTWIFI CONNECTEDWIFI GOT IPOK || WIFI CONNECTEDWIFI GOT IPOK
    //WIFI DISCONNECT << Correct! now need more reads to check the WiFi Status || Can't read anything after that

    char c;
    while(uart_is_readable(UART_ID)){
        printf("%c",uart_getc(UART_ID));
    }

    printf("\nDone\n");
    LED_OFF;

    while (true) {
        sleep_ms(1000);
    }
}