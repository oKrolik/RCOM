#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>

#define FIELD_MAX_LENGTH 512
#define SERVER_PORT 21

//CODES
#define MIN_RETRIEVE 100
#define RETRIEVE_SIZE 150
#define MAX_RETRIEVE 199
#define MIN_TRANSFER 200
#define BINARY 200
#define WELCOME 220
#define GOODBYE 221
#define PASSIVE 227
#define PASSWORD 230
#define USER 331

typedef struct 
{
    char host[FIELD_MAX_LENGTH];
    char ip[FIELD_MAX_LENGTH];
    char user[FIELD_MAX_LENGTH];
    char password[FIELD_MAX_LENGTH];
    char resource[FIELD_MAX_LENGTH];
    char file[FIELD_MAX_LENGTH]; 
} URL;

typedef struct 
{
    char message[1024];
    int code;

} response;

int createSocket(char *ip, int port) {

    int fd;
    struct sockaddr_in server_addr;

    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    if (connect(fd,(struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        return -1;
    }



    return fd;
}


int getIp(char* hostname, URL *url) {
    struct hostent *h;

    if ((h = gethostbyname(hostname)) == NULL) {
        perror("gethostbyname()");
        return -1;
    }

    printf("Host name  : %s\n", h->h_name);
    printf("IP Address : %s\n", inet_ntoa(*((struct in_addr *) h->h_addr)));
    strncpy(url->ip, inet_ntoa(*((struct in_addr *) h->h_addr)), sizeof(url->ip) - 1);
    return EXIT_SUCCESS;
}


int parseUrl(URL *url, char *url_str) {

    regex_t regex;
    regmatch_t matches[7];
    const char *regex_pattern = "^ftp://(([^:@/]+)(:([^@/]+))?@)?([^/]+)/(.+)$";

    if (regcomp(&regex, regex_pattern, REG_EXTENDED) != 0) {
        fprintf(stderr, "Failed to compile Regex.\n");
        return -1;
    }


    if (regexec(&regex, url_str, 7, matches, 0) != 0) {
        fprintf(stderr, "Invalid URL Format.\n");
        regfree(&regex);
        return -1;
    }

    if (matches[1].rm_so != -1) {

        if (matches[2].rm_so != -1) {
            strncpy(url->user, url_str + matches[2].rm_so, matches[2].rm_eo - matches[2].rm_so);
            url->user[matches[2].rm_eo - matches[2].rm_so] = '\0';
        } else {
            strncpy(url->user, "anonymous", sizeof(url->user) - 1);
        }

        if (matches[4].rm_so != -1) {
            strncpy(url->password, url_str + matches[4].rm_so, matches[4].rm_eo - matches[4].rm_so);
            url->password[matches[4].rm_eo - matches[4].rm_so] = '\0';
        } else {
            strncpy(url->password, "password", sizeof(url->password) - 1);
        }
    } else {
        strncpy(url->user, "anonymous", sizeof(url->user) - 1);
        strncpy(url->password, "password", sizeof(url->password) - 1);
    }


    if (matches[5].rm_so != -1) {
        strncpy(url->host, url_str + matches[5].rm_so, matches[5].rm_eo - matches[5].rm_so);
        url->host[matches[5].rm_eo - matches[5].rm_so] = '\0';
    } else {
        url->host[0] = '\0';
    }


    if (matches[6].rm_so != -1) {
        strncpy(url->resource, url_str + matches[6].rm_so, matches[6].rm_eo - matches[6].rm_so);
        url->resource[matches[6].rm_eo - matches[6].rm_so] = '\0';
    } else {
        url->resource[0] = '\0';
    }

   
    char *last_slash = strrchr(url->resource, '/');
    if (last_slash && *(last_slash + 1) != '\0') {     
        strncpy(url->file, last_slash + 1, FIELD_MAX_LENGTH - 1);
        url->file[FIELD_MAX_LENGTH - 1] = '\0';
    } else {
        strncpy(url->file, url->resource, FIELD_MAX_LENGTH - 1);
        url->file[FIELD_MAX_LENGTH - 1] = '\0';
    }

    url->ip[0] = '\0';

    regfree(&regex);

    return EXIT_SUCCESS;
}

int closeSocket(int fd) {
    if(close(fd) < 0) {
        perror("Error closing socket");
        return -1;
    }
    return EXIT_SUCCESS;
}

int readUntilNewline(int fd, char *buf) {
    int res;
    int i = 0;
    do {
        res = read(fd, &buf[i++], 1);
        if (res < 0) return EXIT_FAILURE;
    } while (buf[i - 1] != '\n');

    return EXIT_SUCCESS;
}

void clearResponse(response *newMessage) {
    if (newMessage == NULL) return;
    newMessage->code = 0;
    memset(newMessage->message, 0, sizeof(newMessage->message));
}

void showResponse(response *res) {
    printf("%s", res->message);
}

int receiveResponse(int fd, response *res) {
    int is_num = 1;
    while (1) {

        if (readUntilNewline(fd, res->message) == -1) return EXIT_FAILURE;

        showResponse(res);

        for (int i = 0; i < 3; i++) {
            if (!(res->message[i] >= '0' && res->message[i] <= '9')) {
                is_num = 0;
                break;
            }        
        }


        if ((res->message[3] == ' ') && is_num) {
            res->code = atoi(res->message);
            return EXIT_SUCCESS;
        }

        clearResponse(res);
        is_num = 1;
    }

    return EXIT_SUCCESS;
}


int sendMessage(int fd, response *message){
    if (fd < 0) {
        perror("Invalid socket descriptor");
        return EXIT_FAILURE;
    }

    if (strlen(message->message) == 0) {
        fprintf(stderr, "Invalid message to write\n");
        return EXIT_FAILURE;
    }

    printf("\n%s", message->message);

    size_t bytes_sent = 0;
    size_t to_send = strlen(message->message);

    while (bytes_sent < to_send) {
        ssize_t result = write(fd, message->message + bytes_sent, to_send - bytes_sent);
        if (result < 0) {
            perror("Error writing to socket");
            return EXIT_FAILURE;
        }
        bytes_sent += result;
    }

    response res;
    memset(&res, 0, sizeof(res));

    if (receiveResponse(fd, &res) == -1) {
        perror("receiveResponse failed");
        return EXIT_FAILURE;
    }

    
    strncpy(message->message, res.message, sizeof(message->message) - 1);
    message->message[sizeof(message->message) - 1] = '\0';

    if (message->code == RETRIEVE_SIZE) {
        message->code = res.code;
        return res.code >= MIN_RETRIEVE && res.code <= MAX_RETRIEVE;
    }

    return res.code == message->code;
}

int calculateNewPort(char *passiveMsg, URL url){
    int ip1, ip2, ip3, ip4, port1, port2;

    int parsed = sscanf(passiveMsg, "%*[^(](%d,%d,%d,%d,%d,%d)", &ip1, &ip2, &ip3, &ip4, &port1, &port2);
    
    if (parsed != 6) {
        perror("Failed to parse passive message");
        return EXIT_FAILURE;
    }


    int url_ip1, url_ip2, url_ip3, url_ip4;
    if (sscanf(url.ip, "%d.%d.%d.%d", &url_ip1, &url_ip2, &url_ip3, &url_ip4) != 4) {
        perror("Failed to parse URL IP");
        return EXIT_FAILURE;
    }

    if (ip1 != url_ip1 || ip2 != url_ip2 || ip3 != url_ip3 || ip4 != url_ip4) {
        perror("Mismatched IP addresses");
        return EXIT_FAILURE;
    }

    int port = port1*256 + port2;
    
    return port;
}


int readFile(int fd, char *file, long long file_size) {
    int file_fd;
    off_t bytes_received = 0;
    ssize_t res;

    if ((file_fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        perror("Open file");
        return -1;
    }


    char buf[1024];

    printf("Receiving file: \n");

    while (1) {

        res = read(fd, buf, sizeof(buf));
        
        if (res == 0) {
            break;
        }
        if (res < 0) {

            perror("Failed to read from socket");
            close(file_fd);
            return -1;
        }


        if (write(file_fd, buf, res) < 0) {
            perror("Failed to write to file");
            close(file_fd);
            return EXIT_FAILURE;
        }

        //PROGRESS//
        bytes_received += res;
        float progress = (float)bytes_received / file_size * 100;
        printf("%.2f%%\r", progress);
        fflush(stdout);
    }

    printf("\n[Download Finished]\n");

    close(file_fd);
    return EXIT_SUCCESS;
}

long long getFileSize(char * message) {
    long long bytes = -1;  

    
    char *start = strrchr(message, '(');  
    if (start != NULL) {
        char *end = strchr(start, ')');  
        if (end != NULL) {
            
            *end = '\0';

            
            if (sscanf(start + 1, "%lld", &bytes) == 1) {  
                return bytes;  
            } else {
                printf("Failed to get file size.\n");
            }
        }
    }
    return EXIT_FAILURE;
}


int main(int argc, char *argv[]) {

    if(argc != 2) {
        perror("Invalid download call.");
        return -1;
    }
    
    URL url;
    memset(&url, 0, sizeof(URL));

    if(parseUrl(&url, argv[1]) == -1) {
        perror("parseUrl()");
        return -1;
    }
    printf("User: %s\n", url.user);
    printf("Password: %s\n", url.password);
    printf("Host: %s\n", url.host);
    printf("URL: %s\n", url.resource);
    printf("File: %s\n", url.file);
    
    if(getIp(url.host, &url) == -1) {
        perror("getIp()");
        return -1;
    }
    int fd;
    printf("Creating socket.\n");
    if((fd = createSocket(url.ip, SERVER_PORT)) == -1) {
        perror("createSocket()");
        return -1;
    }
    printf("Socket created.\n");

    response newMessage;
    clearResponse(&newMessage);

    int ret = receiveResponse(fd, &newMessage);
    
    if(ret == -1) {
        if(closeSocket(fd) == -1) return EXIT_FAILURE;
        perror("receiveResponse()");
        return EXIT_FAILURE;
    }

    if(newMessage.code != WELCOME) {
        if(closeSocket(fd) == -1) return -1;
        perror("Server did not respond code 220.");
        return EXIT_FAILURE;
    }    

    printf("[Connected to Server]\n");

    //SEND USER
    clearResponse(&newMessage);
    newMessage.code = USER;
    snprintf(newMessage.message, sizeof(newMessage.message), "USER %s\r\n", url.user);
    
    if(!sendMessage(fd, &newMessage)){
        if(closeSocket(fd) == -1) return -1;
        perror("Failed sending username");
        return EXIT_FAILURE;
    }

    //SEND PASS
    clearResponse(&newMessage);
    newMessage.code = PASSWORD;
    snprintf(newMessage.message, sizeof(newMessage.message), "PASS %s\r\n", url.password);

    if(!sendMessage(fd, &newMessage)){
        if(closeSocket(fd) == -1) return -1;
        perror("Failed sending password");
        return EXIT_FAILURE;
    }

    clearResponse(&newMessage);
    newMessage.code = BINARY;
    strncpy(newMessage.message, "Type I\r\n", sizeof(newMessage.message));

    if(!sendMessage(fd, &newMessage)){
        if(closeSocket(fd) == -1) return -1;
        perror("Failed passing to binary format");
        return EXIT_FAILURE;
    }


    clearResponse(&newMessage);
    newMessage.code = PASSIVE;
    strncpy(newMessage.message, "pasv\r\n", sizeof(newMessage.message));

    if(!sendMessage(fd, &newMessage)){
        if(closeSocket(fd) == -1) return -1;
        perror("Failed activating Passive Mode");
        return EXIT_FAILURE;
    }
    int port2;

    if((port2 = calculateNewPort(newMessage.message, url)) == - 1){
        if(closeSocket(fd) == -1) return -1;
        perror("Failed calculating port");
        return EXIT_FAILURE;
    }
    int fd2;
    if ((fd2 = createSocket(url.ip, port2)) == -1){
        perror("Failed creating socket");
        return EXIT_FAILURE;
    }

    clearResponse(&newMessage);
    newMessage.code = RETRIEVE_SIZE;
    snprintf(newMessage.message, sizeof(newMessage.message), "retr %s\r\n", url.resource);

    if(!sendMessage(fd, &newMessage)){
        if(closeSocket(fd) == -1) return -1;
        perror("Failed retrieving code");
        return EXIT_FAILURE;
    }
    long long fileSize = 1;
    printf("File: %s\n",url.file);
    if (newMessage.code == RETRIEVE_SIZE) {
        fileSize = getFileSize(newMessage.message);
    }
    
    if(fileSize == -1){
        perror("Failed getting file size");
        return EXIT_FAILURE;
    }

    if(readFile(fd2, url.file, fileSize) == -1){
        if(closeSocket(fd) == -1) return -1;
        return EXIT_FAILURE;
    }

    clearResponse(&newMessage);
    
    ret = receiveResponse(fd, &newMessage);

    if(ret == -1) {
        if(closeSocket(fd) == -1) return -1;
        perror("receiveResponse()");
        return EXIT_FAILURE;
    }
    
    if(newMessage.code < MIN_TRANSFER) {
        if(closeSocket(fd) == -1) return -1;
        perror("Transfer failed.");
        return EXIT_FAILURE;
    }    
    
    printf("Transfer completed successfully !\n");

    clearResponse(&newMessage);
    newMessage.code = GOODBYE;
    strncpy(newMessage.message, "quit\r\n", sizeof(newMessage.message));

    if(!sendMessage(fd, &newMessage)){
        if(closeSocket(fd) == -1) return -1;
        perror("Failed sending Goodbye");
        return EXIT_FAILURE;
    }

    if(closeSocket(fd) == -1) return EXIT_FAILURE;
    if(closeSocket(fd2) == -1) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
