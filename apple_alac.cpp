#include <string.h>

// these are headers for the ALAC decoder, utilities and endian utilities
#include <alac/ALACDecoder.h>
#include <alac/ALACBitUtilities.h>
#include <alac/EndianPortable.h>

#include "config.h"
#include "apple_alac.h"

typedef struct magicCookie {
  ALACSpecificConfig config;
  ALACAudioChannelLayout channelLayoutInfo; // seems to be unused
} magicCookie;

magicCookie cookie;
ALACDecoder * theDecoder;

extern "C" int apple_alac_init(int frame_size,int sample_size,int sample_rate) {

  memset(&cookie,0,sizeof(magicCookie));
  
  #define CHANNELS_PER_FRAME  2

  //create a magic cookie for the decoder
  
  cookie.config.frameLength = Swap32NtoB(frame_size);
  cookie.config.compatibleVersion = 0;
  cookie.config.bitDepth = sample_size;
  cookie.config.pb = 40;
  cookie.config.mb = 10;
  cookie.config.kb = 14;
  cookie.config.numChannels = CHANNELS_PER_FRAME;
  cookie.config.maxRun = Swap16NtoB(255);
  cookie.config.maxFrameBytes = 0;
  cookie.config.avgBitRate = 0;
  cookie.config.sampleRate = Swap32NtoB(sample_rate);
  
  theDecoder = new ALACDecoder;
  theDecoder->Init(&cookie, sizeof(magicCookie));  

  return 0;
}

extern "C" int apple_alac_decode_frame(unsigned char *sampleBuffer, uint32_t bufferLength, unsigned char *dest, int *outsize)
{
  uint32_t numFrames = 0;
  BitBuffer theInputBuffer;
  BitBufferInit(&theInputBuffer, sampleBuffer, bufferLength);
  theDecoder->Decode(&theInputBuffer, dest, Swap32BtoN(cookie.config.frameLength), CHANNELS_PER_FRAME, &numFrames);
  *outsize = numFrames;
  return 0;
}

extern "C" int apple_alac_terminate() {
  delete(theDecoder);
}

