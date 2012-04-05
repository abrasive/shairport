#ifndef _AUDIO_H
#define _AUDIO_H

void audio_set_driver(char* driver);
void audio_set_device_name(char* device_name);
void audio_set_device_id(char* device_id);
char* audio_get_driver(void);
char* audio_get_device_name(void);
char* audio_get_device_id(void);
void audio_play(char* outbuf, int samples, void* priv_data);
void* audio_init(int sampling_rate);
void audio_deinit(void);

#endif