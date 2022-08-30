#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"

#include "hal/spi_ll.h"

/*
SPI receiver (slave) example.
This example is supposed to work together with the SPI sender. It uses the standard SPI pins (MISO, MOSI, SCLK, CS) to
transmit data over in a full-duplex fashion, that is, while the master puts data on the MOSI pin, the slave puts its own
data on the MISO pin.
This example uses one extra pin: GPIO_HANDSHAKE is used as a handshake pin. After a transmission has been set up and we're
ready to send/receive data, this code uses a callback to set the handshake pin high. The sender will detect this and start
sending a transaction. As soon as the transaction is done, the line gets set low again.
*/

/*
Pins in use. The SPI Master can use the GPIO mux, so feel free to change these if needed.
*/
//VSPI - SPI3_HOST
#define GPIO_HANDSHAKE 22
#define GB_MISO 23 //19
#define GB_MOSI 19 //23
#define GB_SCLK 18
#define GB_CS 5
#define RCV_HOST SPI3_HOST

#define STACK_SIZE 4096
TaskHandle_t TaskLinkSniffer;
#define PIN_CONTROL 34
bool btnStatus = false;
int teste = 0;
uint8_t image_data[4000] = {}; //1 GBC Picute (5.874)
int array_pointer = 0;

static spi_dev_t *hw = SPI_LL_GET_HW(RCV_HOST);

//Called after a transaction is queued and ready for pickup by master. We use this to set the handshake line high.
void my_post_setup_cb(spi_slave_transaction_t *trans) {
    //gpio_set_level(GPIO_HANDSHAKE, 1);
    //printf("Setup Complete");
}

//Called after transaction is sent/received. We use this to set the handshake line low.
void my_post_trans_cb(spi_slave_transaction_t *trans) {
    //gpio_set_level(GPIO_HANDSHAKE, 0);
    //printf("Data Received");
}


IRAM_ATTR void GbLinkSniffer(void * parameter){
    //portENTER_CRITICAL(&my_mutex);
    esp_err_t ret;

    //Configuration for the SPI bus
    spi_bus_config_t buscfg={
        .mosi_io_num=GB_MOSI,
        .miso_io_num=GB_MISO,
        .sclk_io_num=GB_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1,
        .flags = SPICOMMON_BUSFLAG_SLAVE|SPICOMMON_BUSFLAG_GPIO_PINS,
        .intr_flags = 0 //ESP_INTR_FLAG_IRAM
    };

    //Configuration for the SPI slave interface
    spi_slave_interface_config_t slvcfg={
        .spics_io_num=GB_CS,
        .flags=0,
        .queue_size=1,
        .mode=3,
        .post_setup_cb=my_post_setup_cb,
        .post_trans_cb=my_post_trans_cb
    };

    //Configuration for the handshake line
    gpio_config_t io_conf={
        .intr_type=GPIO_INTR_DISABLE,
        .mode=GPIO_MODE_OUTPUT,
        .pin_bit_mask=(1<<GPIO_HANDSHAKE)
    };

    //Configure handshake line as output
    gpio_config(&io_conf);
    
    gpio_set_pull_mode(GB_MOSI, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GB_MISO, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GB_SCLK, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GB_CS, GPIO_FLOATING);
    gpio_set_pull_mode(GPIO_HANDSHAKE, GPIO_FLOATING);
    gpio_set_level(GPIO_HANDSHAKE,0);

    //Initialize SPI slave interface
    ret=spi_slave_initialize(RCV_HOST, &buscfg, &slvcfg, SPI_DMA_DISABLED);
    assert(ret==ESP_OK);

    hw->pin.ck_idle_edge = 1;
    hw->user.ck_i_edge = 1;
    hw->ctrl2.miso_delay_mode = 0;
    hw->ctrl2.miso_delay_num = 0;
    hw->ctrl2.mosi_delay_mode = 0;
    hw->ctrl2.mosi_delay_num = 0;


    uint8_t sendbuf;
    uint8_t recvbuf;

    spi_slave_transaction_t t;
    memset(&t, 0, sizeof(t));

    while(1) {
        //Clear receive buffer, set send buffer to something sane
        recvbuf=0;
        sendbuf=2;

        //Set up a transaction of 128 bytes to send/receive
        t.length=8;
        t.tx_buffer=&sendbuf;
        t.rx_buffer=&recvbuf;
        /* This call enables the SPI slave interface to send/receive to the sendbuf and recvbuf. The transaction is
        initialized by the SPI master, however, so it will not actually happen until the master starts a hardware transaction
        by pulling CS low and pulsing the clock etc. In this specific example, we use the handshake line, pulled up by the
        .post_setup_cb callback that is called as soon as a transaction is ready, to let the master know it is free to transfer
        data.
        */
        ret=spi_slave_transmit(RCV_HOST, &t, portMAX_DELAY);

        //spi_slave_transmit does not return until the master has done a transmission, so by here we have sent our data and
        //received data from the master. Print it.
        image_data[teste++]=recvbuf;
        //printf("Received: %i\n", recvbuf);
    }
}


void app_main(void)
{
    gpio_set_direction(PIN_CONTROL, GPIO_MODE_INPUT);
    xTaskCreatePinnedToCore(GbLinkSniffer,   // Function that implements the task.
                        "GbLinkSniffer",     // Text name for the task.
                        STACK_SIZE,          // Stack size in bytes, not words.
                        (void *) 1,          // Parameter passed into the task.
                        1,                   // tskIDLE_PRIORITY+1
                        &TaskLinkSniffer,    // Variable to hold the task's data structure.
                        1);
    while(1){
        if (gpio_get_level(PIN_CONTROL) == 1 && !btnStatus){
            btnStatus = true;
            printf("\n");
            printf("%i",teste);
            printf("\n");
            for(int i=0; i<=teste ; i++){
                printf("%i ", image_data[i]);
            } 
            teste=0;
            printf("\n");
        }
        if (gpio_get_level(PIN_CONTROL) == 0){
            btnStatus=false;
        }
        vTaskDelay(100);
    }
}