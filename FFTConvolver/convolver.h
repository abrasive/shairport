// C wrapper to C++ FFTConvolver
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
  
void convolver_init(const char* file, int max_length);
void convolver_process_l(float* data, int length);
void convolver_process_r(float* data, int length);
  
#ifdef __cplusplus
}
#endif
