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

#include "serialization.h"
#include "md5.h"

#ifndef FILESERVER_H
#define FILESERVER_H

class Client {
private:
    static int s_max_id;
    int id;
public:
    Client();
};

struct UploadFileRequest {
    int     fd;
    long    size;
    char    name[252];
};

struct file_information {   // 40bytes
    int     flag;           // 4bytes
    char    filename[64];   // 32bytes
    size_t  filesize;       // 4bytes
};

struct FileRequest {
    int     fd;
    int     flag;
    char    filename[64];
};

int sendAll(int fd, char* data, size_t size);
int recvAll(int fd, char* data, size_t size);
// old version
void createServer(int argc, char *argv[]);
#define MAX_BUFFER_SIZE 1024
class IONetworkBuffer {
private:
    char _buffer[MAX_BUFFER_SIZE];
    char *bptr, *cptr, *eptr;
    char *header, *tail;
public:
    IONetworkBuffer() {
        header = _buffer;
        tail = &_buffer[MAX_BUFFER_SIZE - 1];
        bptr = header;
        cptr = header;
        eptr = header;
    }
    void read();
    void write();
};
class Task;
class FileReceiver;

class FileServer {
protected:
    int                 mPort;
    std::string         mIp;

    int                 listener;
private:
    fd_set                  master;     // master file desciptor list
    fd_set                  read_fds;   // temp file desciptor list for select()
    fd_set                  write_fds;
    int fdmax = 0;
    int                     backlog = 10; // Default max length of wait queues

    struct sockaddr_storage remoteaddr;
    struct addrinfo         hints;
    char                    remoteIP[INET6_ADDRSTRLEN];
    socklen_t               addrlen;


    bool CreateServer();
public:
    Task                    *worker;
    FileReceiver            *receiver;

    FileServer();
    ~FileServer();

    void Listen();
    void Accept();

    void HandleRequest(FileRequest request);
    void HandleUploadRequest(UploadFileRequest request);
};

class Task {
private:
    fd_set _master;
    fd_set _read_fds;
    fd_set _write_fds;
    fd_set _error_fds;
    int fdmax;

    std::mutex _mtx;
    std::vector<FileRequest> _req;
    std::map<int, FILE*> resources;

public:
    Task();
    void run();
    void enqueue(FileRequest req);
};

class FileReceiver {
private:
    fd_set _master;
    fd_set _read_fds;

    int fdmax;

    std::mutex _mtx;
    std::vector<UploadFileRequest> _req;
    std::map<int, FILE*> resources;
    std::map<int, std::shared_ptr<MD5_CTX>> md5_test;
public:
    FileReceiver();

    void enqueue(UploadFileRequest req);
    void run();

};

#endif // FILESERVER_H
