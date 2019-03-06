#define USE_SCTP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef USE_SCTP
#include <netinet/sctp.h>
#endif

#define SIZE 1024
char buf[SIZE];
char *msg = "hello\n";
#define ECHO_PORT 2013

int main(int argc, char *argv[]) {
        int sockfd;
        int nread;
        struct sockaddr_in6 serv_addr;
        /* create endpoint using TCP or SCTP */
        sockfd = socket(AF_INET6, SOCK_STREAM,
#ifdef USE_SCTP
                        IPPROTO_SCTP
#else
                        IPPROTO_TCP
#endif
                );
        if (sockfd < 0) {
                perror("socket creation failed");
                exit(2); }
        /* connect to server */
        serv_addr.sin6_family = AF_INET6;
        serv_addr.sin6_addr = in6addr_loopback;
        serv_addr.sin6_port = htons(ECHO_PORT);
        if (connect(sockfd,
                    (struct sockaddr *) &serv_addr,
                    sizeof(serv_addr)) < 0) {
                perror("connect to server failed");
                exit(3);
        }
        /* write msg to server */
        write(sockfd, msg, strlen(msg) + 1);
        /* read the reply back */
        nread = read(sockfd, buf, SIZE);
        /* write reply to stdout */
        write(1, buf, nread);

        /* exit gracefully */
        close(sockfd);
        exit(0);
}