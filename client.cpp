#include "client.h"
#include "fileserver.h"

#include "md5.h"

using namespace std;

void DieWithError(const char* msg)
{
    std::cout << msg << std::endl;
    exit(-1);
}

void requestFile(string filename, int fd);

///
/// \brief uploadFile send a file to socket `socket`
/// \param socket
/// \param filename
/// \param resume the byte we will start to read
/// \result return the remain bytes were not sent to server
///
long uploadFile(int socket,  const std::string& filename, const std::string& prefix, long resume);

long get_file_size(const char* filename)
{
    FILE *fp;
    fp = fopen(filename, "r");
    if (!fp)
        return 0;

    fseek(fp, 0, SEEK_END);

    int ret =  ftell(fp);
    fclose(fp);
    return ret;
}

void createClient(int argc, char *argv[])
{
    int clientSocket;
    const char ip[] = "192.168.12.126";
    int port = 8080;
    sockaddr_in echoServAddr;
    echoServAddr.sin_addr.s_addr = inet_addr(ip);
    echoServAddr.sin_port = htons(8080);
    echoServAddr.sin_family = AF_INET;

    if ((clientSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        DieWithError("socket() failed");
    }
    if (connect_timeout(clientSocket, (struct sockaddr*) &echoServAddr, sizeof(echoServAddr), 2) < 0) {
        DieWithError("connect() failed");
    }

    std::string req = "face.jpg";
    if (argc > 2) {
        req = argv[2];
    }

    std::string prefix = "";
    if (argc > 3) {
        prefix = argv[3];
    }

    //requestFile(req.c_str(), clientSocket);
    long remainBytes = uploadFile(clientSocket, req, prefix, 0L);
    printf("Remain bytes is %ld\n", remainBytes);
    if (remainBytes > 0) {
        /* try to re-connect server */
        int ntry = 5;
        for (int i = 0; i < ntry; i++) {
            printf("Reconnecting ...\n");
            if (connect(clientSocket, (struct sockaddr*) &echoServAddr, sizeof(echoServAddr)) < 0) {
                printf("Failed! Try after 10s\n");
                perror("connect");
                sleep(10);
                continue;
            }
            remainBytes = uploadFile(clientSocket, req, prefix, remainBytes);
        }
        close(clientSocket);
        DieWithError("Cannot reconnect");

    } else {
        close(clientSocket);
    }
}

void requestFile(string filename, int fd)
{
    if (fd < 0) { // check the file descripter
        return;
    }
    FileRequest req;
    file_information info;
    strcpy(info.filename, filename.c_str());
    req.flag = 1; // request file
    // send the request
    char msg[256];
    strcpy(msg + 1, filename.c_str());
    msg[0] = '1';
    send(fd, msg, sizeof(msg), 0);
    // parse the file info
    printf("File downloading ...\n");
    FILE *fp;
    std::string save_path = "/Users/bbphuc/Desktop/download/";
    std::string download_path = save_path + info.filename;

    std::cout << "> new file is " << download_path << " size " << info.filesize << endl;
    char dl[256];
    sprintf(dl, "%s.%d", download_path.c_str(), getpid());
    fp = fopen(dl, "ab+");
    if (!fp) {
        cout << "> error cannot create file " << download_path << endl;
    }
    // loop until recv enough file size
    size_t num_byte_recv = 0;
    int k = 0;

    unsigned char buff1[32];
    char buff2[1024];
    // Get  first 2 bytes
    long file_size;
    int nbytes = 0;
    nbytes = recv(fd, buff1, sizeof(long), 0);
    if (nbytes > 0) {
        unpack(buff1, "l",&file_size);
    }

    std::cout << "File size is " << file_size << std::endl;

    while (num_byte_recv < file_size) {
        k = recv(fd, buff2, sizeof(buff2), 0);
        if (k > 0) {
            fwrite(buff2, sizeof(char), k / sizeof(char), fp);
            num_byte_recv += k;
        } else  {
            std::cout << "> client error: receiv" << std::endl;
            break;
        }
        std::cout <<"Recv: " << 100. * num_byte_recv / file_size << "%" << endl;
    }
    // close fp
    fclose(fp);
}

void get_md5(unsigned char *digest, const char *path)
{
    FILE *fp;
    fp = fopen(path, "r");
    if (!fp) {
        printf("get_md5() error");
        return;
    }
    MD5_CTX ctx;
    MD5Init(&ctx);
    int nbytes = 0;
    unsigned char buff[1024];
    while ((nbytes = fread(buff, sizeof(unsigned char), 1024, fp)) > 0) {
        MD5Update(&ctx, buff, sizeof(unsigned char) * nbytes);
    }
    MD5Final(digest, &ctx);
}

long uploadFile(int socket,const std::string& filename, const std::string& prefix = std::string(), long resume = 0L)
{
    int     nbytes;
    char    largeBuffer[1024];
    string path = prefix + filename;
    // send the metadata to server to reqest upload file
    FILE *fp;
    fp = fopen(path.c_str(), "r");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    printf("size of file %s is %ld\n", filename.c_str(), sz);
    unsigned char meta[512];
    unsigned char *buff = meta;
    pack(buff, "l", sz);
    buff += 4;
    unsigned char digest[16];
    get_md5(digest, path.c_str());
    memcpy(buff, digest, sizeof(digest));
    buff += 16;
    pack(buff, "s", filename.c_str());
    // send metadata
    sendAll(socket,(char*) meta, sizeof(meta));

    // estimate speed
    timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t s_time = tv.tv_sec * 1000 + tv.tv_usec / 1000; // ms

    // now upload data to server

    MD5_CTX ctx;
    MD5Init(&ctx);

    fseek(fp, 0L, SEEK_SET);
    long totalBytes = sz;
    long totalSentBytes = 0;
    while ((nbytes = fread(largeBuffer, sizeof(char), 1024, fp)) > 0) {
        int sentBytes = 0;
        MD5Update(&ctx, (unsigned char*)largeBuffer, sizeof(unsigned char) * nbytes);
        do {
            int val =  send(socket, &largeBuffer[sentBytes], nbytes - sentBytes, 0);
            if (val < 0 ) {
                perror("send");
                // reconnect to server
                return totalBytes - totalSentBytes;
            }
            totalSentBytes += val;
            sentBytes += val;
        } while (sentBytes < nbytes);
    }
    // close file pointer
    unsigned char mm[16];
    MD5Final(mm, &ctx);
    for (int n = 0; n < 16; ++n) {
        printf("%02x", (unsigned int)mm[n]);
    }
    printf("\n");
    gettimeofday(&tv, NULL);
    uint32_t e_time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    double u_speed = (totalSentBytes / 1024.0) / (e_time - s_time) * 1000;
    printf("Upload load speed is %.3f\n KB/s", u_speed);

    fclose(fp);
    return 0;
}

static void connect_alarm(int signo);

int connect_timeout(int sockfd, const sockaddr* sa, socklen_t sa_len, unsigned int timeout)
{
    Sigfunc* sigfunc;
    sigfunc = signal(SIGALRM, connect_alarm);

    if (alarm(timeout) != 0) {
        printf("error: alarm was already set\n");
    }
    int n;
    if ((n = connect(sockfd, sa, sa_len)) < 0) {
        close(sockfd);
        if (errno == EINTR) {
            errno = ETIMEDOUT;
        }
    }
    alarm(0);
    signal(SIGALRM, sigfunc);

    return (n);
}

static void connect_alarm(int signo)
{
    printf("timeout \n");
    return;
}
