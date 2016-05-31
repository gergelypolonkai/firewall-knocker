#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "config.h"

int
main(void)
{
    int sockfd;
    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct addrinfo *p;
    int rv;
    int connected;

    // Set the hints
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Get address info (AKA resolv hostname)
    if ((rv = getaddrinfo(SERVER_ADDRESS, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // Cycle OUTER
    while (1) {
        int con;

        // Here we assume that we are not connected.
        connected = 0;

        printf("Connecting...\n");

        // Create a socket
        for (p = servinfo; p != NULL; p = p->ai_next) {
            if ((sockfd = socket(p->ai_family,
                                 p->ai_socktype,
                                 p->ai_protocol)) == -1) {
                perror("client: socket");

                continue;
            }

            break;
        }

        // If p is NULL, we didn't succeed to create a socket. This
        // should never happen
        if (p == NULL) {
            fprintf(stderr, "client: failed to connect\n");

            return 2;
        }

        // Set the socket to non-blocking so we can control the timeout value
        fcntl(sockfd, F_SETFL, O_NONBLOCK);

        // Start the connection (it usually won't succeed yet)
        con = connect(sockfd, p->ai_addr, p->ai_addrlen);

        // If connect() returned an error saying the connection is in progress
        if ((con < 0) && (errno == EINPROGRESS)) {
            // Cycle INNER1
            while (1) {
                struct timeval tv;
                int t;
                fd_set master;

                // Create an empty descriptor set, and add our socket to it
                FD_ZERO(&master);
                FD_SET(sockfd, &master);

                // Set the timeout value to SENDING_FREQ seconds
                tv.tv_sec = SENDING_FREQ;
                tv.tv_usec = 0;

                // Run the select()
                t = select(sockfd + 1, NULL, &master, NULL, &tv);

                if ((t < 0) && (errno != EINTR)) {
                    // Some serious error happened, let's exit
                    perror("select");

                    exit(1);
                } else if (t < 0) {
                    // select() was interrupted, lets restart the INNER1 cycle
                    printf("select() interrupted, continue\n");

                    continue;
                } else if (t > 0) {
                    size_t lon;
                    int valopt;

                    lon = sizeof(int);
                    if (getsockopt(sockfd,
                                   SOL_SOCKET, SO_ERROR,
                                   (void*)(&valopt), &lon) < 0) {
                        fprintf(stderr,
                                "Error in getsockopt() %d - %s\n",
                                errno, strerror(errno));
                        connected = 0;
                        sleep(SENDING_FREQ);

                        break;
                    }
                    // Check the value returned...
                    if (valopt) {
                        fprintf(stderr,
                                "Error in delayed connection() %d - %s\n",
                                valopt, strerror(valopt));
                        connected = 0;
                        sleep(SENDING_FREQ);

                        break;
                    }

                    printf("Connected with select().\n");
                    connected = 1;

                    break;
                } else if (t == 0) {
                    // Connection timed out, let's break out from INNER1
                    printf("connect() timed out.\n");
                    connected = 0;
                    close(sockfd);

                    break;
                }
            }
        } else if (con < 0) {
            // connect() error. Wait SENDING_FREQ seconds and try
            // again (restarting the OUTER cycle)

            // XXX error handling?
            printf("connect() error, retry\n");
            sleep(SENDING_FREQ);
            connected = 0;

            continue;
        } else if (con == 0) {
            // We are now connected. Usually we won't get here, but in
            // the select() loop above instead
            printf("Connected without select().\n");
            connected = 1;
        }

        if (connected) {
            // If we are connected, let's jump in the INNER2 cycle
            while (1) {
                fd_set read_fds;
                int t;
                struct timeval tv;
                char buf[MAXDATASIZE];

                // Create and empty descriptor set and add our socket
                FD_ZERO(&read_fds);
                FD_SET(sockfd, &read_fds);

                // Set the timeout value to SENDING_FREQ seconds
                tv.tv_sec = SENDING_FREQ;
                tv.tv_usec = 0;

                // Let's run the select()
                t = select(sockfd + 1, &read_fds, NULL, NULL, &tv);

                if ((t < 0) && (errno != EINTR)) {
                    // select() ran into an error, this is bad. Let's exit
                    perror("select");

                    exit(1);
                } else if (t < 0) {
                    // select() interrupted, try again by restarting
                    // the INNER2 cycle
                    printf("select() interrupted\n");

                    continue;
                } else if (t > 0) {
                    // We got some data from the server
                    size_t len;

                    printf("Data from server.\n");

                    len = recv(sockfd, &buf, MAXDATASIZE, 0);

                    if (len <= 0) {
                        // If the received data length is at most 0,
                        // we are disconnected, so break out from
                        // INNER2
                        printf("Closing connection.\n");
                        connected = 0;
                        close(sockfd);

                        break;
                    }
                }

                // If we arrive here, select() ran into timeout, so we
                // should send some data to the server.
                printf("Sending data to server\n");
                send(sockfd, "!\n", 2, 0);
            }
        }
    }

    // This should never happen, but if so, let's clean up after ourselved
    freeaddrinfo(servinfo);

    close(sockfd);

    return 0;
}
