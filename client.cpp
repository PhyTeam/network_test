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
void uploadFile(int socket,  const std::string& filename);

void createClient(int argc, char *argv[])
{
    int clientSocket;
    const char ip[] = "127.0.0.1";
    int port = 8080;
    sockaddr_in echoServAddr;
    echoServAddr.sin_addr.s_addr = inet_addr(ip);
    echoServAddr.sin_port = htons(8080);
    echoServAddr.sin_family = AF_INET;

    if ((clientSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        DieWithError("socket() failed");
    }

    if (connect(clientSocket, (struct sockaddr*) &echoServAddr, sizeof(echoServAddr)) < 0) {
        DieWithError("connect() failed");
    }
    std::string req = "face.jpg";
    if (argc > 2) {
        req = argv[2];
    }

    //requestFile(req.c_str(), clientSocket);
    uploadFile(clientSocket, req);
    close(clientSocket);
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
    int nbytes = recv(fd, buff1, sizeof(long), 0);
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

void uploadFile(int socket,  const std::string& filename)
{
    int     nbytes;
    char    largeBuffer[1024];
    const string prefix = "/Users/bbphuc/Desktop/res/";
    string path = prefix + filename;
    // send the metadata to server to reqest upload file
    FILE *fp;
    fp = fopen(path.c_str(), "r");
    if (!fp) {
        perror("fopen");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    printf("size of file %s is %ld\n", filename.c_str(), sz);
    unsigned char meta[512];
    unsigned char *buff = meta;
    pack(buff, "l", sz);
    buff += 4;
    pack(buff, "s", filename.c_str());
    // send metadata

    send(socket, meta, sizeof(meta), 0);
    // now upload data to server

    MD5_CTX ctx;
    MD5Init(&ctx);

    fseek(fp, 0L, SEEK_SET);
    while ((nbytes = fread(largeBuffer, sizeof(char), 1024, fp)) > 0) {
        int sentBytes = 0;
        MD5Update(&ctx, (unsigned char*)largeBuffer, sizeof(unsigned char) * nbytes);
        do {
            int val =  send(socket, &largeBuffer[sentBytes], nbytes - sentBytes, 0);
            if (val < 0 ) {
                perror("send");
                return;
            }
            sentBytes += val;

        } while (sentBytes > nbytes);
        //printf("%d / %ld have been sent to the server.\n", nbytes, sz);
    }
    // close file pointer
    unsigned char digest[16];
    MD5Final(digest, &ctx);
    for (int n = 0; n < 16; ++n) {
        printf("%02x", (unsigned int)digest[n]);
    }
    printf("\n");

    fclose(fp);
}
