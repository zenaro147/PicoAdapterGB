#pragma GCC optimize ("O3")
    
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"
#include "freertos/portmacro.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include <soc/rtc.h>
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/timer.h"
#include "esp32/rom/uart.h"
#include "esp_task_wdt.h"


#define GB_MISO 19 // output
#define GB_MOSI 23 // input
#define GB_SCLK 18 // input

#define PIN_CONTROL 34 // input
bool btnStatus = false;
int teste = 0;

#define STACK_SIZE 4096

uint8_t image_data[12000] = {}; //1 GBC Picute (5.874)
int array_pointer = 0;

static uint8_t recv_data = 0;
//static uint8_t send_data = 0;
static uint8_t recv_bits = 0;

static portMUX_TYPE my_mutex = portMUX_INITIALIZER_UNLOCKED;


IRAM_ATTR void gpio_callback(uint gpio, uint32_t events){
  // check gpio
  if (gpio != GB_SCLK) return;
  
  // on the falling edge set sending bit
  if (events & 0) {
    //gpio_put(PIN_SOUT, send_data & 0x80), send_data <<= 1;
    return;
  }
  
  // on the rising edge read received bit
  recv_data = (recv_data << 1) | ((GPIO.in >> GB_MOSI) & 0x1);
  if (++recv_bits != 8) return;
  recv_bits = 0;
  image_data[array_pointer++] = recv_data;

  //send_data = process_data(recv_data);
}

IRAM_ATTR void GbLinkSniffer(void * parameters){
  portENTER_CRITICAL(&my_mutex);
  bool new_clk, old_clk = (GPIO.in >> GB_SCLK) & 0x1;
  while(true){
    portDISABLE_INTERRUPTS();
    if ((new_clk = ((GPIO.in >> GB_SCLK) & 0x1)) != old_clk) {
      gpio_callback(GB_SCLK, (new_clk) ? HIGH : LOW);
      old_clk = new_clk;
    }
    portENABLE_INTERRUPTS();
  }
}


void Task_2_cp(void * parameters){
  while(1){
    if (gpio_get_level(PIN_CONTROL) == 1 && !btnStatus){
      btnStatus = true;
      printf("\n");
      printf("Total: %i \n", array_pointer);
      for(int i=0; i<=array_pointer ; i++){
        printf("%i \n", image_data[i]);
      } 
      printf("\n");
    } 
    if (gpio_get_level(PIN_CONTROL) == 0){
      btnStatus=false;
    }
    vTaskDelay(1);
  }
}



void app_main(void)
{
    gpio_set_direction(PIN_CONTROL, GPIO_MODE_INPUT);
    gpio_set_direction(GB_SCLK, GPIO_MODE_INPUT);
    gpio_set_direction(GB_MOSI, GPIO_MODE_INPUT);
    gpio_set_direction(GB_MISO, GPIO_MODE_OUTPUT);
    gpio_set_level(GB_MISO,0);

    TaskHandle_t TaskLinkSniffer;
    xTaskCreatePinnedToCore(GbLinkSniffer,       // Function that implements the task.
                            "GbLinkSniffer",     // Text name for the task.
                            STACK_SIZE,          // Stack size in bytes, not words.
                            (void *) 1,        // Parameter passed into the task.
                            1,                   //tskIDLE_PRIORITY+1
                            &TaskLinkSniffer,    // Variable to hold the task's data structure.
                            1);


    TaskHandle_t Task_2;
    xTaskCreatePinnedToCore(Task_2_cp,       // Function that implements the task.
                            "Task_2_cp",     // Text name for the task.
                            STACK_SIZE,          // Stack size in bytes, not words.
                            (void *) 1,        // Parameter passed into the task.
                            tskIDLE_PRIORITY+2,                   //tskIDLE_PRIORITY+1
                            &Task_2,    // Variable to hold the task's data structure.
                            0);
}

