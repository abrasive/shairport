#pragma once

#include <stdio.h>

typedef struct {
  float a0, a1, a2, b1, b2;
  float i1, i2, o1, o2;
} loudness_processor;

extern loudness_processor loudness_r;
extern loudness_processor loudness_l;

void loudness_set_volume(float volume);
float loudness_process(loudness_processor *p, float sample);
