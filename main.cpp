#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <string>
#include <ctime>

#include <thread>

#include "fileserver.h"
#include "client.h"
#include "json.h"

using namespace std;

#define MAXPENDING 10 // backlog
#define MAX_BUFFER 1024
#define LOOP(n) for (int i = 0; i < n; ++i)

#define LOG_D(tag, msg) printf("Debug client: %s : %s", tag, msg)

// rtt
double rtt = -1; // in s
double timeval_subtract(struct timeval *x, struct timeval *y)
{
    double diff = x->tv_sec - y->tv_sec;
    diff += (x->tv_usec - y->tv_usec)/1000000.0;

    return diff;
}

/* measure rtt (with weighed moving average).
    cur_ts - start_ts is the time between request sent and first response socket.read() */
double measure_rtt(struct timeval *start_ts, struct timeval *cur_ts)
{
    double cur_rtt = timeval_subtract(cur_ts,start_ts);
    if(rtt < 0)
    {
        // first measurement
        rtt = cur_rtt;
    }
    else
    {
        // weighed moving average
        rtt = 0.8*rtt + 0.2*cur_rtt;
    }
    return rtt;
}

class Message;
class TCP_Client {
private:
    int             fd;

    std::string     ip;
    int             port;

    bool            is_connected;
    unsigned char   buffer[1024];

public:
    TCP_Client();

    void Connect(const std::string& ip, int port);
    void Send(const Message& msg);
    void Send(const char* data, size_t size);
    int Read(char* data, size_t size);
    void ReadInput();
    void Disconnect();
};

class Message {
    int flag;
    std::time_t tm;
    std::string text;
    std::string sender;
    std::string receiver;
public:
    Message() {

    }

    Message(const std::string& text, const std::string& sender, const std::string& receiver) {
        this->text = text;
        this->sender = sender;
        this->receiver = receiver;
        this->tm = std::time(nullptr); // Get current time of this system
    }

    int Pack(unsigned char *buff) const {
        char format[] = "llsss";
        int size = 0;
        size += pack(buff, format, 0, flag, text.c_str(), sender.c_str(), receiver.c_str());
        return size;
    }

    unsigned int Unpack(unsigned char* buff) {
        char _t[1024], _s[1024], _r[1024];
        char _format[] = "llsss";
        long none;
        unsigned int read = unpack(buff, _format, &none, &flag, _t, _s, _r);
        this->text = _t;
        this->sender = _s;
        this->receiver = _r;
        return read;
    }

    std::string ToString() const {
        std::stringstream ss;
        ss << " > " << text  << " at " << std::asctime(std::localtime(&tm));
        return ss.str();
    }

    std::string ToJson() const {
        // Convert this message to json
        stringstream ss;
        ss << "{\"sender\":\"" << sender << "\", \"receiver\":" << receiver;
        return ss.str();
    }
};

TCP_Client::TCP_Client(): is_connected(false)
{

}

void TCP_Client::Connect(const std::string &ip, int port)
{
    sockaddr_in sa;
    sa.sin_addr.s_addr = inet_addr(ip.c_str());
    sa.sin_port = htons(port);
    sa.sin_family = AF_INET;

    // Change file descriptor to non-blocking
//    int val = fcntl(F_GETFL, fd);
//    fcntl(F_SETFL, fd, O_NONBLOCK | val);

    if ((fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        DieWithError("socket() failed");
    }
    if (::connect(fd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
    //if (connect_timeout(fd, (struct sockaddr*) &sa, sizeof(sa), 2) < 0) {
        DieWithError("connect() failed");
    }
    // Connected
    is_connected = true;
    this->port = port;
}

void TCP_Client::Send(const Message& msg)
{
    bzero(buffer, sizeof(buffer));
    int nbytes = msg.Pack((unsigned char*)buffer);
    pack(buffer, "l", (long)nbytes);
    std::cout << "Pack size " << nbytes << std::endl;

    int sbytes = 0;
    do {
        int n = send(fd, &buffer[sbytes], nbytes - sbytes, 0);
        if (n < 0) {
            LOG_D("send", "Sending message failed.");
        } else {
            sbytes += n;
        }
    } while (sbytes < nbytes);
}

void TCP_Client::Send(const char* buff, size_t nbytes)
{
    size_t sbytes = 0;
    int n = 0;
    do {
       n = send(fd, &buff[sbytes], nbytes - sbytes, 0);
       if (n < 0) {
           LOG_D("send", "failed");
       } else {
           sbytes += n;
       }
    } while (sbytes < nbytes);
}

void TCP_Client::ReadInput() {
    // Read input from stdin
    char buff[1024];
    // Read until reach the new line characters
    int n = read(0, buff, sizeof(buff));

    if (n <= 0) {

    } else {

    }
}

int TCP_Client::Read(char *data, size_t size)
{
    int rbytes = 0;
    rbytes = recvAll(fd, data, size);
    return rbytes;
}

void TCP_Client::Disconnect()
{
    close(fd); // Close connection
}

class TCP_Server {
private:
    int listener;

    fd_set master;
    fd_set read_fds;
    int fdmax;

    struct sockaddr_storage remoteaddr;
    struct addrinfo hints;
    char remoteIP[INET6_ADDRSTRLEN];
    // socklen_t addrlen;

    int backlog;
public:
    TCP_Server();
    virtual ~TCP_Server();

    void Listen(int port);
    void Accept();
protected:

    virtual void onConnected();
    virtual void onDisconnected();
    virtual void onReceivedData(void* data, size_t size, int socket);
};

TCP_Server::TCP_Server() {}
TCP_Server::~TCP_Server() {}

void TCP_Server::Listen(int port) {
    struct addrinfo *ai, *p;
    int i, nbytes;
    unsigned char _buff[10240];
    unsigned int  _rbuff_len = 0;
    unsigned char _rbuff[1024];
    memset(&hints, 0, sizeof(hints));
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    hints.ai_family     = AF_UNSPEC;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_flags      = AI_PASSIVE;

    char _p[32];
    sprintf(_p, "%d", port);
    int rv;
    if ((rv = getaddrinfo(NULL, _p, &hints, &ai))) {
        fprintf(stderr, "server error: %s\n", gai_strerror(rv));
        return;
    }

    for (p = ai; p != NULL;p = p->ai_next) {
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
        return;
    }

    int val = fcntl(listener, F_GETFL, 0);
    fcntl(listener, F_SETFL, val | O_NONBLOCK); // Set the listener file description to non-blocking

    fprintf(stdout, "server > info: %s\n", "Server has been created.");
    freeaddrinfo(ai); // free the memory

    ::listen(listener, backlog);
    FD_SET(listener, &master);
    fdmax = listener;

    // main loop
    for(;;) {
        read_fds = master; // copy it
        if (select(fdmax+1, &read_fds, NULL , NULL, NULL) == -1) {
            if (errno == EAGAIN) { // server run out of resources
                std::this_thread::sleep_for(std::chrono::seconds(10));
                continue;
            } else if (errno == EBADF) { // check file description
                for (i = 0; i <= fdmax; i++) {
                    if (FD_ISSET(i, &master)) {
                        int val = fcntl(F_GETFD, i);
                        if (val < 0) {
                            FD_CLR(i, &master);
                        }
                    }
                }
                continue;
            } else { // Invalid arguments
                break;
            }
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    Accept();
                } else {
                    // handle data from a client
                    if (_rbuff_len > 0)
                        memcpy(_buff, _rbuff, _rbuff_len);
                    if ((nbytes = recv(i, _buff + _rbuff_len, sizeof(_buff) - _rbuff_len, 0)) <= 0) {
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
                        onReceivedData(_buff, nbytes, i);
                    }
                } // END handle data from client
            } // END got new incoming connection
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!

}

void TCP_Server::Accept() {
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

void TCP_Server::onConnected() {

}

void TCP_Server::onDisconnected() {
    std::cout << "Client has been disconnected." << std::endl;
}

void TCP_Server::onReceivedData(void *data, size_t size, int socketfd)
{
    (void) data;
    (void) socketfd;
    printf("Recv data. size: %zu \n", size);
    // send ack to the client

#if 0
    // Concate last data
    int r = 0;
    long msg_size = 0;
    do {
        unpack(_buff + r, "l", &msg_size);
        printf("Recv msg has len: %ld, %d\n", msg_size, nbytes  - r);
        if ((msg_size > 0) && (msg_size > nbytes - r)) break;

        Message msg;
        msg.Unpack(_buff + r);
        r += msg_size;
        std::cout << msg.ToString();
    } while (msg_size > 0 && (nbytes - r) > 0);


    if ((_rbuff_len = nbytes - r) > 0)
        memcpy(_rbuff, _buff + r, _rbuff_len);
    else
        _rbuff_len = 0;
#endif
}

class EchoServer: public TCP_Server
{
public:
    EchoServer();
    ~EchoServer();

protected:
    void onReceivedData(void *data, size_t size, int socketfd);
};

EchoServer::EchoServer() {}
EchoServer::~EchoServer() {}

void EchoServer::onReceivedData(void *data, size_t size, int socketfd)
{
    TCP_Server::onReceivedData(data, size, socketfd);
    // Echo back to the client
    int ret = -1;
    int delay = 1;

    ret = sendAll2(socketfd, (char*) data, size);

    printf("> Echo to the client number %d", socketfd);
}

int main(int argc, char *argv[])
{
    int i;
    if (argc >= 2) {
        i = atoi(argv[1]);
    } else {
        i = 1;
    }
    char *ip = NULL;
    char default_ip[] = "127.0.0.1";
    std::string prefix = "/Users/bbphuc/Desktop/upload/";
    if (argc >= 3) {
        // Get prefix arguments
        //prefix = argv[2];
	ip = argv[2];
    } else {
        ip = default_ip;
	}

    switch (i) {
    case 1:
    {
        FileReceiver receiver;
        std::thread t([&]() {
            receiver.run(prefix);
        });

        FileServer server;
        server.receiver = &receiver;

        server.Listen();
        t.join();
        break;
    }
    case 3:
    {
        TCP_Client client;
        client.Connect(ip, 8080);
        size_t size = 1 << 20;

        char* data = (char*) malloc(size);
        bzero(data, size);
        timeval tv_s; // the time at begin of packet
        timeval tv_c; // the current time
        LOOP(100) {
            gettimeofday(&tv_s, NULL);
            client.Send(data, size);
            int ret = client.Read(data, size);
            gettimeofday(&tv_c, NULL);
            printf("RTT: %.5f ms, bytes: %d\n", timeval_subtract(&tv_c, &tv_s) * 1000., ret);
        }
        free(data);
        client.Disconnect();
        break;
    }
    case 4:
    {
        EchoServer server;
        server.Listen(8080);
        break;
    }
    default:
        break;
    }
    return 0;
}
