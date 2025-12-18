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



void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ;  // wait for serial port to connect. Needed for native USB port only
  }

  Serial.println("--- SETUP ---");
  //PDM.begin(2,3);

}

void loop() {
  Serial.println("--- LOOP ---");

  int cmd = getIntegerNonBlocking();

  if(cmd == 1) {
    Serial.println("ON");
    PDM.begin(2,3);
  } else if(cmd == 0) {
    Serial.println("OFF");
    PDM.end();
  }

  delay(1000);

}

