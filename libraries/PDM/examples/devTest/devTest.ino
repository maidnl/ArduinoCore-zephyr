#include "PDM.h"


PDMClass PDM;
/* -------------------------------------------------------------------------- */
int getIntegerNonBlocking() {
/* -------------------------------------------------------------------------- */
   /* basic function that get a integer from serial 
   * (can be improved....)
   * it does not wait for input from serial and return -1 if*/

  int rv = -1;
  if(Serial.available()) {
    rv = 0;
  }
  
  while(Serial.available()) {
    int num = Serial.read();
    
    if( (num >= 0x30 && num <= 0x39) ) {
      rv *= 10;
      rv += num - 0x30;
    }
    else if(num == 0x4D || num == 0x6D) {
      while(Serial.available()) {
        Serial.read();
      }
      return num;
    }
  }
  return rv;
}

short sampleBuffer[1024];

// Number of audio samples read
volatile int samplesRead;
void on_receive() {

  // Query the number of available bytes
  int bytesAvailable = PDM.available();
  Serial.print("** RX ** ");

  Serial.print(bytesAvailable);
  Serial.print(" ");
  Serial.println(samplesRead);

  // Read into the sample buffer
  PDM.read(sampleBuffer, bytesAvailable);



  // 16-bit, 2 bytes per sample
  samplesRead = bytesAvailable;

}

void task() {
  static bool st = false;  
static unsigned long t = millis();
  if(millis() -t > 1000) {
    t = millis();
    if(!st) {
      digitalWrite(LED_BUILTIN,HIGH);
      st = true;
    } else {
      digitalWrite(LED_BUILTIN,LOW);
      st = false;
      
    }

  }

}

void setup() {
  pinMode(LED_BUILTIN,OUTPUT);
  //Initialize serial and wait for port to open:
  Serial.begin(115200);
  while (!Serial) {
    ;  // wait for serial port to connect. Needed for native USB port only
  }

  Serial.println("--- SETUP ---");
  //PDM.begin(2,3);

  PDM.onReceive(on_receive);


}

void loop() {
  //Serial.println("--- LOOP ---");

  task();
  int cmd = getIntegerNonBlocking();

  if(cmd == 1) {
    Serial.println("ON");
    PDM.begin(1,16000);
  } else if(cmd == 0) {
    Serial.println("OFF");
    PDM.end();
  }

  // Wait for samples to be read
  if (samplesRead) {

    // Print samples to the serial monitor or plotter
    Serial.print("samples read: ");
    Serial.println(samplesRead);
    // Clear the read count
    samplesRead = 0;

  }

}

