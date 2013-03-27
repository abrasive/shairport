#ifndef _AUDIO_H
#define _AUDIO_H

typedef struct {
    // start of program
    int (*init)(int argc, char **argv);
    // at end of program
    void (*deinit)(void);

    void (*start)(int sample_rate);
    // block of samples
    void (*play)(short buf[], int samples);
    void (*stop)(void);

    // may be NULL, in which case soft volume is applied
    void (*volume)(double vol);
} audio_ops;

extern audio_ops audio_dummy;
extern audio_ops audio_ao;

#endif //_AUDIO_H
