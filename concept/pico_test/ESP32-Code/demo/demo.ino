#define RXD2 16
#define TXD2 17

#include <HardwareSerial.h>
HardwareSerial SerialPort(2); // use UART2

String command;
int output;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  SerialPort.begin(115200, SERIAL_8N1, RXD2, TXD2);
}

void loop() {
  if(SerialPort.available()){
    Serial.print("Data Received:" );
    command = SerialPort.readString();    
    Serial.println(command);
    SerialPort.print("xxOK");
  }
  Serial.println(command);
  output = command.length();
  Serial.println(output);
  Serial.println(command.indexOf("AT"));
  delay(1000);
}
