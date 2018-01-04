#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>

#include <iostream>
#include <string>

#include <fcntl.h>

#include <vector>
#include <queue>
#include <map>

#include <time.h>
#include <sstream>

#include "fileserver.h"

#ifndef CLIENT_H
#define CLIENT_H

void DieWithError(const char* msg);

void createClient(int argc, char *argv[]);

typedef void Sigfunc(int);
int connect_timeout(int sockfd, const sockaddr* sa, socklen_t sa_len, unsigned timeout);


#endif // CLIENT_H
