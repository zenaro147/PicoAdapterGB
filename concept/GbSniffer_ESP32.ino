#include "soc/rtc_wdt.h"
#include "esp_int_wdt.h"
#include "esp_task_wdt.h"

#define GB_MOSI 23
#define GB_MISO 19
#define GB_SCLK 18
  
uint8_t image_data[12000] = {}; //1 GBC Picute (5.874)
int array_pointer = 0;

static uint8_t recv_data = 0;
static uint8_t send_data = 0;
static uint8_t recv_bits = 0;

TaskHandle_t TaskLinkSniffer;
static portMUX_TYPE my_mutex = portMUX_INITIALIZER_UNLOCKED;



IRAM_ATTR void GbLinkSniffer(void *pvParameters){
  portENTER_CRITICAL(&my_mutex);
  bool new_clk, old_clk = (GPIO.in >> GB_SCLK) & 0x1;
  new_clk = old_clk;
  while(true){
    portDISABLE_INTERRUPTS();
    if ((new_clk = ((GPIO.in >> GB_SCLK) & 0x1)) != old_clk) {
      gpio_callback(GB_SCLK, (new_clk) ? RISING : FALLING);
      old_clk = new_clk;
    }
    portENABLE_INTERRUPTS();
  }
}

IRAM_ATTR void gpio_callback(uint gpio, uint32_t events){
  // check gpio
  if (gpio != GB_SCLK) return;
  
  // on the falling edge set sending bit
  if (events & FALLING) {
    //gpio_put(PIN_SOUT, send_data & 0x80), send_data <<= 1;
    return;
  }
  
  // on the rising edge read received bit
  recv_data = (recv_data << 1) | ((GPIO.in >> GB_MOSI) & 0x1);
  //bitWrite(recv_data, 7-recv_bits, ((GPIO.in >> GB_MOSI) & 0x1));
  
  if (++recv_bits != 8) return;
  recv_bits = 0;
  image_data[array_pointer++] = recv_data;

  //send_data = process_data(recv_data);
}

static uint8_t process_data(uint8_t data_in) {
  //do something
}




void setup() {
  Serial.begin(115200);  
  
  //function takes the following frequencies as valid values:
  //  240, 160, 80    <<< For all XTAL types
  //  40, 20, 10      <<< For 40MHz XTAL
  //  26, 13          <<< For 26MHz XTAL
  //  24, 12          <<< For 24MHz XTAL
  setCpuFrequencyMhz(240);
  
  //disable Watchdog
  rtc_wdt_protect_off(); 
  rtc_wdt_disable();
  disableCore0WDT();
  //disableCore1WDT();
  disableLoopWDT();
  

  // put your setup code here, to run once:
  pinMode(GB_SCLK, INPUT);
  pinMode(GB_MOSI, INPUT);
  pinMode(GB_MISO, OUTPUT);  
  digitalWrite(GB_MISO, LOW);   
  
  xTaskCreatePinnedToCore(GbLinkSniffer,    // Task function. 
                          "GbLinkSniffer",  // name of task. 
                          4096,             // Stack size of task 
                          NULL,             // parameter of the task -- (void*)&img_tmp
                          1,                // priority of the task 
                          &TaskLinkSniffer, // Task handle to keep track of created task 
                          0);               // pin task to core 0 
}

void loop() {
  // put your main code here, to run repeatedly:
  while (Serial.available() > 0)
  {
    switch (Serial.read())
    {
      case '1':      
        Serial.println(array_pointer);
        for (int i = 0; i <= array_pointer; i++) {
          for (int x = 7; x >= 0; x--){
            bool b = bitRead(image_data[i], x);
            Serial.print(b);
          }
          Serial.println(' ');
        }
        resetVars();
        Serial.println(' ');
        break;  
      case '2':      
        Serial.println(array_pointer);
        for (int i = 0; i <= array_pointer; i++) {
          Serial.print(image_data[i],HEX);
          Serial.print(' ');
        }
        resetVars();
        Serial.println(' ');
        break;     
    }
  }
}

void resetVars(){
  memset(image_data, 0x00, sizeof(image_data));
  array_pointer = 0;
  recv_bits = 0;
  recv_data = 0;
  send_data = 0;
}
