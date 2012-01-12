#ifndef __SHAIRPORT_H__
#define __SHAIRPORT_H__

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "socketlib.h"
#include <regex.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <regex.h>


#define HWID_SIZE 6
#define SHAIRPORT_LOG 1
#define LOG_INFO     1
#define LOG_DEBUG    5
#define LOG_DEBUG_V  6
#define LOG_DEBUG_VV 7

struct shairbuffer
{
  char *data;
  int   current;
  int   maxsize;
  int   marker;
};

struct keyring
{
  char *aeskey;
  char *aesiv;
  char *fmt;
};

struct comms
{
  int  in[2];
  int  out[2];
};

struct connection
{
  struct shairbuffer  recv;
  struct shairbuffer  resp;
  struct keyring      *keys; // Does not point to malloc'd memory.
  struct comms        *hairtunes;
  int                 clientSocket;
  char                *password;
};

RSA *loadKey();
int startAvahi(const char *pHwAddr, const char *pServerName, int pPort);
void cleanupBuffers(struct connection *pConnection);
void cleanup(struct connection *pConnection);
void handleClient(int pSock, char *pPassword, char *pHWADDR);
int getAvailChars(struct shairbuffer *pBuf);

char *getTrimmedMalloc(char *pChar, int pSize, int pEndStr, int pAddNL);
char *getTrimmed(char *pChar, int pSize, int pEndStr, int pAddNL, char *pTrimDest);
void initBuffer(struct shairbuffer *pBuf, int pNumChars);
void printBufferInfo(struct shairbuffer *pBuf, int pLevel);
void addToShairBuffer(struct shairbuffer *pBuf, char *pNewBuf);
void addNToShairBuffer(struct shairbuffer *pBuf, char *pNewBuf, int pNofNewBuf);

int readDataFromClient(int pSock, struct shairbuffer *pClientBuffer);
int  parseMessage(struct connection *pConn,unsigned char *pIpBin, unsigned int pIpBinLen, char *pHWADDR);

void closePipe(int *pPipe);
void setKeys(struct keyring *pKeys, char *pIV, char* pAESKey, char *pFmtp);
void initConnection(struct connection *pConn, struct keyring *pKeys, 
                struct comms *pComms, int pSocket, char *pPassword);

void writeDataToClient(int pSock, struct shairbuffer *pResponse);
void propogateCSeq(struct connection *pConn);
int buildAppleResponse(struct connection *pConn, unsigned char *pIpBin, unsigned int pIpBinLen, char *pHwAddr);
void sim(int pLevel, char *pValue1, char *pValue2);
void slog(int pLevel, char *pFormat, ...);
int  isLogEnabledFor(int pLevel);

#endif
