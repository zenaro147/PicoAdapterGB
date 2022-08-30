# Some Dev notes using Pi Pico

* The 'data_test.sal' file has an example of the transmission results between GB and Pico using an Logical Analyzer to sniff the GB link cable data. You can open this file using [this software](https://www.saleae.com/downloads/)
* The current timeout timer was set to 200ms and completely resets the SPI (using spi0 port) (need this to clear the buffer and prevent to send some "unused data" at the first Stream byte)
* During the SPI reset, a little "oscillation" occurs in the clock signal. Actually, I don't know how to prevent that or if this could interfere in the Mobile Adapter functionality. (this oscillation lasts 30.5uS (microseconds) )
* Baudrate on ESP eeprom 'UART_CUR:115273,8,1,0,1'... Baudrate from Pico: 115176
* Maybe PIO could also be a workaround? (In the worst scenario)

