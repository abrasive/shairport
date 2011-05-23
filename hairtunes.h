#ifndef _HAIRTUNES_H_
#define _HAIRTUNES_H_
int hairtunes_init(char *pAeskey, char *pAesiv, char *pFmtpstr, int pCtrlPort, int pTimingPort,
         int pDataPort, char *pRtpHost, char*pPipeName, char *pLibaoDriver, char *pLibaoDeviceName, char *pLibaoDeviceId);

// default buffer size
#define BUFFER_FRAMES  320

#endif 
