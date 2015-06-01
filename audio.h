#ifndef _AUDIO_H
#define _AUDIO_H

#include <stdint.h>
#include <libconfig.h>

typedef struct {
  double airplay_volume;
  double current_volume_dB;
  int minimum_volume_dB;
  int maximum_volume_dB;
  int has_true_mute;
  int is_muted;
  int valid;
} audio_parameters;

typedef struct {
    void (*help)(void);
    char *name;

    // start of program
    int (*init)(int argc, char **argv);
    // at end of program
    void (*deinit)(void);

    void (*start)(int sample_rate);
    
    // block of samples
    void (*play)(short buf[], int samples);
    void (*stop)(void);
    
    // may be null if not implemented
    void (*flush)(void);
    
    // returns the delay before the next frame to be sent to the device would actually be audible.
    // almost certainly wrong if the buffer is empty, so put silent buffers into it to make it busy.
    // will change dynamically, so keep watching it. Implemented in ALSA only.
    uint32_t (*delay)();

    // may be NULL, in which case soft volume is applied
    void (*volume)(double vol);
    
    // may be NULL, in which case soft volume parameters are used
    void (*parameters)(audio_parameters* info);
} audio_output;

audio_output *audio_get_output(char *name);
void audio_ls_outputs(void);

#endif //_AUDIO_H
