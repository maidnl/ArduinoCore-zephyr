#ifndef ARDUINO_ZEPHYR_PDM_H
#define ARDUINO_ZEPHYR_PDM_H

#include <Arduino.h>
#include <cstdint>
#include "./utility/PDMDoubleBuffer.h"

namespace arduino {

class PDMClass
{
public:
  PDMClass();
  PDMClass(int pwrPin);
  virtual ~PDMClass();

  int begin(int channels, int sampleRate);
  void end();

  virtual int available();
  virtual int read(void* buffer, size_t size);

  void onReceive(void(*)(void));

  void setGain(int gain);
  size_t getBufferSize();

// private:

private:
  bool active;
  PDMDoubleBuffer db;
  int _pwrPin;

  int set_up_double_buffer();
  int set_up_double_buffer_1();
  
  void (*_onReceive)(void);
};

}

typedef arduino::PDMClass PDMClass;

//extern PDMClass PDM;


#endif // ARDUINO_ZEPHYR_PDM_H

