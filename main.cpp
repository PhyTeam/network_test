#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <string>

#include <thread>

#include "fileserver.h"
#include "client.h"
using namespace std;

#define MAXPENDING 10 // backlog
#define MAX_BUFFER 1024

int main(int argc, char *argv[])
{

    int i;
    if (argc >= 2) {
        i = atoi(argv[1]);
    } else {
        i = 1;
    }

    switch (i) {
    case 1:
    {
        FileReceiver receiver;
        std::thread t([&]() {
            receiver.run();
        });

        FileServer server;
        server.receiver = &receiver;
        server.Listen();

        t.join();
        break;
    }
    case 2:
    {
        // fork 10 client proccess
        int pid, wpid;
        int status;
        for (int  i = 0; i < 5; i++) {
            if ((pid = fork()) == 0) { // child proccess
                createClient(argc, argv);
                break;
            } else { // parent proccess
                continue;
            }
        }
        while ((wpid = wait(&status)) > 0) {
            printf("Exit status of proccess %d is %d\n", wpid, status);
        }
    }
    default:
        break;
    }
    return 0;
}
