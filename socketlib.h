/* Name:  M. Andrew Webster
 * Based on stevens networking book
 * Description: function prototypes for socket library
 */

#ifndef _SOCKETLIB_H
#define _SOCKETLIB_H

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>


#define  MAX_SIZE 8192
#define  PORT 5000
#define  RETRY_COUNT 7
#define  SERVLEN 80
#define  RETRY_DELAY 1000

#define ERROR -1
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_UNIX "/unix"

int setup_client(struct addrinfo *server_info);
int setup_server(struct addrinfo *server_address);
int setupListenServer(struct addrinfo **pAddrInfo, int pPort);
int acceptClient(int pSock, struct addrinfo *server_addr);
void delay(long pMillisecs, struct timeval *pRes);
int getAddr(char *pHostname, char *pService, int pFamily, int pSockType, struct addrinfo **pAddrInfo);

// All calls to decode and encode need to be freed
char *decode_base64(unsigned char *input, int length, int *tActualLength);
// All calls to decode and encode need to be freed
char *encode_base64(unsigned char *input, int length);


#endif
