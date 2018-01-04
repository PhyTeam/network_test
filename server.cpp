#include "fileserver.h"
#include "client.h"

#include <sys/stat.h>
#include <thread>

#include <errno.h>
using namespace std;

#define PORT "9034"   // port we're listening on
#define MAXPENDING 10
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int sendAll(int fd, char* data, size_t size)
{
    int i = 0;
    while (i < size) {
        int nbytes = send(fd, data + i, size - i, 0);
        if (nbytes > 0) {
            i += nbytes;
        } else {
            fprintf(stderr, "err: can not sent message\n");
            return i;
        }
    }
    return i;
}

int recvAll(int fd, char* data, size_t size)
{
    int r = 0;
    while (r < size) {
        int nbytes = recv(fd, &data[r], size - r, 0);
        if (nbytes > 0) {
            r += nbytes;
        } else {
            fprintf(stderr, "err: can not recv full message\n");
            return r;
        }
    }
    return r;
}

FileServer::FileServer() {
    // Init the default port and ip
    this->mPort = 8080;
    this->mIp = "127.0.0.1";

    CreateServer();
}
FileServer::~FileServer() {}

bool FileServer::CreateServer() {
    struct addrinfo *ai, *p;

    memset(&hints, 0, sizeof(hints));
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    hints.ai_family     = AF_UNSPEC;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_flags      = AI_PASSIVE;
    char port[32];
    sprintf(port, "%d", this->mPort);
    int rv;
    if ((rv = getaddrinfo(NULL, port, &hints, &ai))) {
        fprintf(stderr, "server error: %s\n", gai_strerror(rv));
        return false;
    }

    for (p = ai; p != NULL; p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }
        int yes = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (::bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stdout, "server error: can not binding\n");
        return false;
    }

    int val = fcntl(listener, F_GETFL, 0);
    fcntl(listener, F_SETFL, val | O_NONBLOCK); // Set the listener file description to non-blocking

    fprintf(stdout, "server > info: %s\n", "Server has been created.");
    freeaddrinfo(ai); // free the memory
}

void FileServer::Listen() {
    int i, j;
    int nbytes;
    unsigned char _buff[512];

    ::listen(listener, backlog); // listening from the socket
    FD_SET(listener, &master);
    fdmax = listener;

    // main loop
    for(;;) {
        read_fds = master; // copy it
        write_fds = master;
        if (select(fdmax+1, &read_fds, &write_fds, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    Accept();
                } else {
                    // handle data from a client
                    if ((nbytes = recv(i, _buff, sizeof(_buff), 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) {
                            // connection closed
                            printf("selectserver: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                    } else {
                        // create request for the message
                        unsigned char *ptr = _buff;
                        long flag, size;
                        unpack(ptr, "l", &size);
                        ptr += 4;

                        unsigned char md5[16];
                        memcpy(md5, ptr, sizeof(md5));
                        ptr += 16;

                        char filename[256];
                        unpack(ptr, "%s", filename);

                        printf("size of file %s is %ld\n", filename,size);
                        UploadFileRequest req;
                        req.fd = i;
                        req.size = size;
                        memcpy(req.md5, md5, sizeof(md5));

                        strcpy(req.name, filename);
                        HandleUploadRequest(req);
                        FD_CLR(i, &master); // do not listening this client
                    }
                } // END handle data from client
            } // END got new incoming connection
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!
}

void FileServer::Accept() {
    // handle new connections
    socklen_t addrlen = sizeof remoteaddr;
    int newfd = accept(listener,
        (struct sockaddr *)&remoteaddr,
        &addrlen);

    // Change the flags of new socket to non-blocking
    int val = fcntl(newfd, F_GETFL, 0);
    fcntl(newfd, F_SETFL, val | O_NONBLOCK);

    if (newfd == -1) {
        perror("accept");
    } else {
        FD_SET(newfd, &master); // add to master set
        if (newfd > fdmax) {    // keep track of the max
            fdmax = newfd;
        }

        printf("selectserver: new connection from %s on "
            "socket %d\n",
            inet_ntop(remoteaddr.ss_family,
            get_in_addr((struct sockaddr*)&remoteaddr),
                remoteIP, INET6_ADDRSTRLEN),
                newfd);
    }
}

void FileServer::HandleRequest(FileRequest request) {
   std::cout << "Request has been received, file request is " << request.filename << std::endl;
   std::string path = "/Users/bbphuc/Desktop/res/";
   path += request.filename;

   FILE *fp;
   fp = fopen(path.c_str(), "r");
   fseek(fp, 0L, SEEK_END);
   long lSize = ftell(fp);
    // Send total bytes should receive
   unsigned char buffer[32];
   pack(buffer, "l", lSize);
   send(request.fd, buffer, sizeof(long), 0);
   FileRequest req;

   int val = fcntl(request.fd, F_GETFL, 0);
   fcntl(request.fd, F_SETFL, val | O_NONBLOCK); // Set the listener file description to non-blocking


   if(worker) {
       worker->enqueue(request);
   }

   fclose(fp);
}

void FileServer::HandleUploadRequest(UploadFileRequest request) {
    // Upload the request for receiver
    receiver->enqueue(request);
}

///////////////////////////////////////////////////////////
Task::Task() : fdmax(0) {
    FD_ZERO(&_master);
    FD_ZERO(&_read_fds);
    FD_ZERO(&_write_fds);
    FD_ZERO(&_error_fds);
}

void Task::enqueue(FileRequest req) {
    std::lock_guard<std::mutex> lk(_mtx);
    _req.push_back(req);
    FD_SET(req.fd, &_master);
    if (fdmax < req.fd) {
        fdmax = req.fd;
    }
}

void Task::run() {
    char buffer[2048];
    while (true) {
        _read_fds = _master;
        _write_fds = _master;
        _error_fds = _master;
        struct timeval tv = {1 , 0 };

        if (_req.empty()) {
            continue;
        }

        if (select(fdmax + 1, NULL, &_write_fds, NULL, &tv) == -1) {
            perror("select");
            exit(4);
        }

        for (int i = 0; i <= fdmax; ++i) {
            if (FD_ISSET(i, &_write_fds)) {
                // Check the file has already to read
                auto it = resources.find(i);
                if (it == resources.end()) {
                    // Open resources
                    FILE *fp;
                    char *filename;
                    for (int j = 0; j < _req.size(); ++j) {
                        if (_req[j].fd == i)
                            filename = _req[j].filename;
                    }
                    char prefix[] = "/Users/bbphuc/Desktop/res/";
                    char *full_path = strcat(prefix, filename );
                    fp = fopen(full_path, "r+");
                    if (!fp) {
                        perror("fopen");
                    }
                    // open file
                    std::cout << "Open file " << full_path  << ((fp) ? "Successfully" : "Failed ")<< std::endl;
                    resources.insert(std::pair<int, FILE*>(i, fp));

                } else  {
                    // Read the file into the buffer
                    //std::cout << "Write on " << i << std::endl;
                    FILE* fp = (*it).second;
                    if (fp) {
                        int c = fread(buffer, sizeof(char), 2048, fp);
                        // Write into the socket buffer
                        if (c == 0) {
                            resources.erase(it);
                            fclose(fp);
                            FD_CLR(i, &_master);
                        } else {
                            write(i, buffer, sizeof(char) * c);
                        }
                    }
                }
            }
        }
    }
}
///////////////////////////////////////////////////////////
FileReceiver::FileReceiver() : fdmax(0), _IsContinue(true) {
    FD_ZERO(&_master);
    FD_ZERO(&_read_fds);
}

void FileReceiver::enqueue(UploadFileRequest req) {
    std::lock_guard<std::mutex> lk(_mtx);
    _req.push_back(req);
    FD_SET(req.fd, &_master);
    fdmax = std::max(req.fd, fdmax);
    printf("push %s to receiver sock %d\n", req.name, req.fd);
}

void FileReceiver::run(const std::string& prefix) {

    char buffer[2048];

    while (_IsContinue) {// Loop forever
        _read_fds = _master;
        struct timeval tv = {0, 500 };

        if (_req.empty()) {
            continue;
        }

        if (select(fdmax + 1, &_read_fds, NULL, NULL, &tv) == -1) {
            perror("select");
            if (errno == EAGAIN) { // server run out of rerources
                printf("SERVER: Server run out of resources");
                std::this_thread::sleep_for(std::chrono::seconds(2)); // Sleep for 2s and try again
                continue;
            } else if (errno == EBADF) { // One of fd was invalid
                for (int i = 0; i <= fdmax; i++) {
                    if (FD_ISSET(i, &_master)) {
                        // Check whether fd is valid
                        if (fcntl(i, F_GETFD) == -1) {
                            printf("Remove invalid fd %d\n", i);
                            // Remove the invalid fd
                            FD_CLR(i, &_master);
                        }
                    }
                }
                continue;
            } else {
                // Invalid arguments
                exit(-1);
            }

        }

        for (int i = 0; i <= fdmax; ++i) {
            if (FD_ISSET(i, &_read_fds)) {
                // Check the file has already to read
                auto it = resources.find(i);
                if (it == resources.end()) {
                    // Open resources
                    FILE *fp;
                    char *filename;
                    static int _idx = 0;
                    for (int j = 0; j < _req.size(); ++j) {
                        if (_req[j].fd == i)
                            filename = _req[j].name;
                    }
                    //char prefix[] = "/Users/bbphuc/Desktop/upload/";
                    char full_path[256];
                    sprintf(full_path,"%s%s%d", prefix.c_str(), filename, _idx++);
                    fp = fopen(full_path, "w+");
                    if (!fp) {
                        perror("fopen");
                    }
                    // open file
                    std::cout << "Write file " << full_path  << ((fp) ? " Ok!" : " Failed!!!")<< std::endl;
                    resources.insert(std::pair<int, FILE*>(i, fp));
                    auto ctx = make_shared<MD5_CTX>(MD5_CTX());
                    MD5Init(ctx.get());
                    md5_test.insert(std::pair<int, shared_ptr<MD5_CTX>>(i, ctx));

                } else  {
                    // Read the file into the buffer
                    FILE* fp = (*it).second;
                    if (fp) {
                        int nbytes = recv(i, buffer, sizeof(buffer), 0);
                        if (nbytes == 0) { // client stop send data
                            unsigned char digest[16];

                            MD5Final(digest, (md5_test.find(i)->second).get());
                            printf("Checking md5 hash: ");
                            for (int n = 0; n < 16; ++n) {
                                printf("%02x", (unsigned int)digest[n]);
                            }
                            printf("\n");
                            // find the request
                            auto ret = std::find_if(_req.begin(), _req.end(), [&](const UploadFileRequest& req) {
                                return (req.fd == i);
                            });
                            assert(ret != _req.end());
                            int n = memcmp((*ret).md5, digest, sizeof(digest));
                            if (n == 0) {
                                printf("MD5 check: Successfull\n");
                            } else {
                                printf("MD5 check: Failed\n");
                            }

                            printf("uploading was stopped by client\n");
                            resources.erase(it);
                            fclose(fp);
                            FD_CLR(i, &_master);
                        } else if (nbytes > 0) {
                            shared_ptr<MD5_CTX> ctx = (md5_test.find(i)->second);
                            fwrite(buffer, sizeof(char), nbytes, fp);
                            MD5Update(ctx.get(), (unsigned char*) buffer, nbytes * sizeof(char));
                        } else {
                            std::cout << "error:  can not recv" << std::endl;
                        }
                    }
                }
            }
        }
    }
}

void FileReceiver::stop()
{
    _IsContinue = false;
}

void FileReceiver::resume()
{
    _IsContinue = true;
}

///////////////////////////////////////////////////////////
/// Old version
///////////////////////////////////////////////////////////

void createServer(int argc, char *argv[])
{
    int serverSocket;
    struct sockaddr_in echoServAddr;

    echoServAddr.sin_family = AF_INET;
    echoServAddr.sin_port = htons(8080);
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY);


    if ((serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        DieWithError("socket() failed");
    }
    int echoServAddrLen = sizeof(echoServAddr);
    if (::bind(serverSocket, (struct sockaddr*) &echoServAddr, echoServAddrLen) < 0) {
        DieWithError("bind() failed");
    }

    if (listen(serverSocket, MAXPENDING) < 0) {
        DieWithError("listen() failed");
    }

    if (argc < 3) {
        DieWithError("Input the file path in arguments");
    }
    char *path = argv[2];
    string filepath(path);
    // get file name
    string filename = filepath.substr(filepath.find_last_of("\\/") + 1, filepath.length() - 1);

    struct stat result;

    stat(path, &result);
    std::cout << " The size of file " << path <<
                     " is  " << result.st_size << std::endl;


    FILE* fp;
    fp = fopen(path, "r");
    if (!fp) {
        std::cout << "Error: Cannot read file " << path << endl;
    }

    std::cout << "> Server listening client to connect ..." << std::endl;

    for (;;)
    {
        int clientSocket;
        sockaddr_in echoClientAddr;
        socklen_t clntLen = sizeof(echoClientAddr);
        if ((clientSocket = accept(serverSocket, (struct sockaddr*) &echoClientAddr, &clntLen)) < 0) {
            DieWithError("accept() failed");
        }
        char str[100];
        inet_ntop(AF_INET, &(echoClientAddr.sin_addr),  str, sizeof(str));

        std::cout << "> New client connected. ip: "<< str << " port: " << ntohs(echoClientAddr.sin_port) << std::endl;
        pid_t pid;
        if ((pid = fork()) > 0) { // parent process
            std::cout << "> child proccess has been created. pid : " << pid << std::endl;
            close(clientSocket);
        } else if (pid == 0) { // child proccess
            /* send file information to the new client */
            file_information fi;
            strcpy(fi.filename, filename.c_str());
            fi.filesize = result.st_size;
            send(clientSocket, &fi, sizeof(fi), 0);

            /* send file to the new connected client */
            fseek(fp, 0, SEEK_SET);
            char buffer[2048];
            size_t read_bytes;
            while ((read_bytes = fread(buffer, sizeof(char), 2048, fp)) > 0) {
                // Send this chunk of data to the client
                size_t sent_bytes = send(clientSocket, buffer, sizeof(char) * read_bytes, 0);
                if (sent_bytes != sizeof(char) * read_bytes) {
                    std::cout << "missing " << std::endl;
                }
            }
            close(clientSocket);
            return;
        } else {
            std::cout << "> error: fork() failed" << endl;
            exit(0);
        }
    }
    close(serverSocket); // release the socket
}

