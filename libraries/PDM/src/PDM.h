#ifndef ARDUINO_ZEPHYR_PDM_H
#define ARDUINO_ZEPHYR_PDM_H

#include <Arduino.h>
#include <cstdint>
#include "./utility/PDMDoubleBuffer.h"

#include <zephyr/audio/dmic.h>
/* size in bit of an audio sample */
/* NANO 33 BLE will work only if this value is 16 */
#define SAMPLE_BIT_WIDTH 16

namespace arduino {

class PDMClass
{
public:
  PDMClass();
  virtual ~PDMClass();
  /* support 1 or 2 channels, sampleRate can be 16000 or 41667 */
  int begin(int channels, int sampleRate);
  void end();

  virtual int available();
  virtual int read(void* buffer, size_t size);

  void onReceive(void(*)(void));

  void setGain(int gain);
  size_t getBufferSize();


private:
  bool configured;
  bool active;
  PDMDoubleBuffer db;
  struct pcm_stream_cfg stream;
  struct dmic_cfg cfg;
};

}

typedef arduino::PDMClass PDMClass;

//extern PDMClass PDM;


#endif // ARDUINO_ZEPHYR_PDM_H

