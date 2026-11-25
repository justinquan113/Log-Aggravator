#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include "common.h"


int main(){

    /* Implementation of UNIX Server - Daemon */
    char server_message[REPLY_MAX] = "You have reached the shadow server";

    /*'Create server socket'*/
    int server_socket;
    server_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /*'Define server address'*/
    unlink(SOCK_PATH);
    struct sockaddr_un server_address = {0};
    server_address.sun_family = AF_LOCAL;
    strncpy(server_address.sun_path, SOCK_PATH, sizeof(server_address.sun_path) - 1);

    /*'Bind the socket to our specified address'*/
    int bind_status = bind(server_socket, (struct sockaddr*) &server_address, sizeof(server_address));
    if (bind_status < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    /* Second arguement, says how many connections can be waiting for this socket at one time*/
    listen(server_socket, 3);

    int client_socket;
    
    printf("Daemon started...\n");
    printf("Waiting for client...\n");

    while (1) {
        client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        printf("Client connected!\n");
        char buffer[MAX_LINE] = {0}; 

        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("Received: %s\n", buffer);
        }

        // To add: Process instructions from client and send back data once task/data is recieved

        send(client_socket, server_message, sizeof(server_message), 0);

        close(client_socket);
    }

    close(server_socket);
    unlink(SOCK_PATH);

    return 0;
}