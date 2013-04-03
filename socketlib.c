/*
 * Socket Library - Common socket functions, based on stevens networking book.
 * Copyright (c) M. Andrew Webster 2011
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#include <stdio.h>
#include "socketlib.h"
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

int common_setup(struct addrinfo *pAddrInfo)
{  
  int tSock;
  //printAddrs(pAddrInfo);
  tSock = socket(pAddrInfo->ai_family, pAddrInfo->ai_socktype, 0);
#ifdef AF_INET6
  if((tSock==-1) && (pAddrInfo->ai_family == AF_INET6) && (errno == EAFNOSUPPORT))
  {
    //Fallback to ipv4
    perror("Failed to create ipv6 socket. Trying ipv4");
    pAddrInfo->ai_family = AF_INET;
    tSock = socket(pAddrInfo->ai_family, pAddrInfo->ai_socktype, 0);
  }
#endif
  return tSock;
}

int setup_client(struct addrinfo *server_host)
{
  int tSockDesc = -1;
  int tIdx = 0;

  while(tIdx++ < RETRY_COUNT)
  {
    tSockDesc = common_setup(server_host);
    if (tSockDesc < 0 && tIdx >= RETRY_COUNT)
    {
      perror("Error: Could not create socket");
      return ERROR;
    }

    if (connect(tSockDesc, server_host->ai_addr, server_host->ai_addrlen) >= 0)
    {
      return tSockDesc;
    }
    else
    {
      close(tSockDesc);
      perror("Error: Could not connect to server");
      struct timeval tRes;
      delay(RETRY_DELAY, &tRes);
    }
  }
  printf("%d Retry attempts exceeded\n", RETRY_COUNT);
  return ERROR;
}

int getAddr(char *pHostname, char *pService, int pFamily, int pSockType, struct addrinfo **pAddrInfo)
{
  struct addrinfo hints;
  int tError = 0;
  memset(&hints, 0 , sizeof(hints));

  hints.ai_family = pFamily;
  hints.ai_socktype = pSockType;
  if(pHostname == NULL)
  {
    hints.ai_flags = AI_PASSIVE;
  }

  tError = getaddrinfo(pHostname, pService, &hints, pAddrInfo);
  if(tError != 0)
  {
    printf("Error getting address info\n");
  }
  return tError;
}

int setup_server(struct addrinfo *server_addr)
{
  int tSock = common_setup(server_addr);
  if (tSock < 0)
  {
    perror("Error: Could not create server socket");
    return ERROR;
  }

  int tEnable = 1;
  setsockopt(tSock, SOL_SOCKET, SO_REUSEADDR, &tEnable, sizeof (tEnable));
  if (bind(tSock, server_addr->ai_addr, server_addr->ai_addrlen) < 0)
  {
    close(tSock);
    perror("Error: Could not bind socket");
    return ERROR;
  }

  if (listen(tSock, 5) < 0)
  {
    close(tSock);
    perror("Error: Unable to listen on server socket");
    return ERROR;
  }

  return tSock;
}


int acceptClient(int pSock, struct addrinfo *server_addr)
{
  int tAccept = accept(pSock, server_addr->ai_addr, &server_addr->ai_addrlen);
  // close the listen socket.  Not expecting any more clients.
  if (tAccept < 0)
  {
    perror("Error: Unable to accept connection to server socket");
    return ERROR;
  }
  else
  {
    //printf("..Accepted on socket: %d\n", tAccept);
  }
  return tAccept;
}

int setupListenServer(struct addrinfo **pAddrInfo, int pPort)
{
    char tService[SERVLEN];
    sprintf(tService, "%d", pPort); // copies port to string
    int tFamily = AF_INET;
    #ifdef AF_INET6
    //printf("Listening on IPv6 Socket\n");
    tFamily = AF_INET6;
    #else
    //printf("Listening on IPv4 Socket");
    #endif
    if(getAddr(NULL, tService, tFamily, SOCK_STREAM, pAddrInfo))
    {
      return ERROR; // getAddr prints out error message
    }

    int tSocketDescriptor = setup_server(*pAddrInfo);
    char tAddr[INET6_ADDRSTRLEN];
    socklen_t tSize = INET6_ADDRSTRLEN;
    inet_ntop((*pAddrInfo)->ai_family, (*pAddrInfo)->ai_addr, tAddr, tSize);
    //printf("Size is: %d\n", tSize);
    return tSocketDescriptor;
}

void delay(long pMillisecs, struct timeval *pRes)
{
  pRes->tv_sec = pMillisecs / 1000;
  pRes->tv_usec = (pMillisecs - (pRes->tv_sec * 1000)) * 1000;
  select(0,NULL,NULL,NULL,pRes);
}

static int getCorrectedEncodeSize(int pSize)
{
  if(pSize % 4 == 0)
  {
    return pSize;
  }
  else if((pSize + 1) % 4 == 0)
  {
    return pSize+1;
  }
  else if((pSize + 2) % 4 == 0)
  {
    return pSize+2;
  }
  else
  {
    // Invalid encoded data, no other cases are possible.
    printf("Unrecoverable error....base64 values are incorrectly encoded\n");
    return pSize;
  }
}

// From http://www.ioncannon.net/programming/34/howto-base64-encode-with-cc-and-openssl/

//int main(int argc, char **argv)
//{
//  char *output = decode_base64("WU9ZTyEA\n\0", strlen("WU9ZTyEA\n\0"));
//  printf("Unbase64: *%s*\n", output);
//  free(output);/
//}

char *decode_base64(unsigned char *pInput, int pLength, int *pActualLength)
{
  // Needs All NO_NL flags for proper RSA AES KEY Descrypt
  BIO *b64, *bmem;
  unsigned char *input = pInput;
  int length = getCorrectedEncodeSize(pLength);
  if(pLength != length)
  {
    input = malloc(length * sizeof(unsigned char));
    memset(input, 0, length);
    memcpy(input, pInput, pLength);
    memset(input+pLength, '=', length-pLength);
    printf("Fixed value: [%.*s]\n", length, input);
  }
  char *buffer = (char *)malloc(length);
  memset(buffer, 0, length);

  b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

  bmem = BIO_new_mem_buf(input, length);
  BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);
  bmem = BIO_push(b64, bmem);

  BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);

  *pActualLength = BIO_read(bmem, buffer, length);

  BIO_free_all(bmem);

  if(pLength != length)
  {
    free(input);
  }
  return buffer;
}

char *encode_base64(unsigned char *input, int length)
{
  BIO *bmem, *b64;
  BUF_MEM *bptr;

  b64 = BIO_new(BIO_f_base64());
  // This enables/disables nls
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  bmem = BIO_new(BIO_s_mem());

  b64 = BIO_push(b64, bmem);

  BIO_write(b64, input, length);
  (void)BIO_flush(b64);
  BIO_get_mem_ptr(b64, &bptr);

  char *buff = (char *)malloc(bptr->length);
  memcpy(buff, bptr->data, bptr->length-1);
  buff[bptr->length-1] = 0;

  BIO_free_all(b64);

  return buff;
}
