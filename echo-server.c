#define USE_SCTP

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef USE_SCTP
#include <netinet/sctp.h>
#endif

#define SIZE 1024
char buf[SIZE];
#define ECHO_PORT 2013

int main(int argc, char *argv[]) {
        int sockfd, client_sockfd;
        int nread, len;
        struct sockaddr_in6 serv_addr, client_addr;

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
                exit(2);
        }
        /* bind address */
        serv_addr.sin6_family = AF_INET6;
        serv_addr.sin6_addr = in6addr_any;
        serv_addr.sin6_port = htons(ECHO_PORT);
        if (bind(sockfd,
                 (struct sockaddr *) &serv_addr,
                 sizeof(serv_addr)) < 0) {
                perror("bind failed");
                exit(3); }
        /* specify queue length */
        listen(sockfd, 5);
        for (;;) {
                len = sizeof(client_addr);
                /* get a connection from client */
                client_sockfd = accept(sockfd,
                                       (struct sockaddr *) &client_addr,
                                       &len);
                if (client_sockfd == -1) {
                        perror("accept failed");
                        continue;
                }
                /* transfer data */
                nread = read(client_sockfd, buf, SIZE);
                /* write to stdout */
                write(1, buf, nread);
                /* and echo it back to client */
                write(client_sockfd, buf, nread);
                /* no more for this client */
                close(client_sockfd);
        }
}
