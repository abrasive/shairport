

#include "convolver.h"
#include <sndfile.h>
#include "FFTConvolver.h"
#include "Utilities.h"
extern "C" {
#include "../common.h"
}


static fftconvolver::FFTConvolver convolver_l;
static fftconvolver::FFTConvolver convolver_r;


void convolver_init(const char* filename, int max_length)
{
  SF_INFO info;
  assert(filename);
  SNDFILE* file = sf_open(filename, SFM_READ, &info);
  assert(file);
  
  if (info.samplerate != 44100)
    die("Impulse file \"%s\" sample rate is %d Hz. Only 44100 Hz is supported", filename, info.samplerate);
  
  if (info.channels != 1 && info.channels != 2)
    die("Impulse file \"%s\" contains %d channels. Only 1 or 2 is supported.", filename, info.channels);
  
  const size_t size = info.frames > max_length ? max_length : info.frames;
  float buffer[size*info.channels];
  
  size_t l = sf_readf_float(file, buffer, size);
  assert(l == size);
  
  if (info.channels == 1) {
    convolver_l.init(352, buffer, size);
    convolver_r.init(352, buffer, size);
  } else {
    // deinterleave
    float buffer_l[size];
    float buffer_r[size];
    
    int i;
    for (i=0; i<size; ++i)
    {
      buffer_l[i] = buffer[2*i+0];
      buffer_r[i] = buffer[2*i+1];
    }
    
    convolver_l.init(352, buffer_l, size);
    convolver_r.init(352, buffer_r, size);
  }
  
  debug(1, "IR initialized from \"%s\" with %d channels and %d samples", filename, info.channels, size);
  
  sf_close(file);
}

void convolver_process_l(float* data, int length)
{
  convolver_l.process(data, data, length);
}

void convolver_process_r(float* data, int length)
{
  convolver_r.process(data, data, length);
}
