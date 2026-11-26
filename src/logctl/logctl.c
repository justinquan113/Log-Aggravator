#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include "common.h"

/*Need to learn how to handle connections errors*/

int get_command(char *command, size_t buffer_size) {

    char prompt[] = "logctl> ";
    char error[] = "Please enter a shorter command\n";

    while (1) {
        size_t index = 0;
        char ch;
        ssize_t bytes_read;

        write(STDOUT_FILENO, prompt, sizeof(prompt) - 1);

        while ((bytes_read = read(STDIN_FILENO, &ch, 1)) == 1
               && ch != '\n') {

            if (index < buffer_size - 1) {
                command[index++] = ch;
            } else {
                write(STDERR_FILENO, error, sizeof(error) - 1);

                while ((bytes_read = read(STDIN_FILENO, &ch, 1)) == 1
                       && ch != '\n');

                index = 0;
                break;
            }
        }

        if (bytes_read <= 0) {
            return 0;
        }

        command[index] = '\0';

        if (index == 0) {
            continue;
        }

        return 1;
    }
}

/* Ensures all the data is sent via the socket to the server */
int send_command(int socket_fd, const char *command, size_t length) {

    size_t total_sent = 0;

    while (total_sent < length) {

        ssize_t bytes_sent = send(
            socket_fd,
            command + total_sent,
            length - total_sent,
            0
        );

        if (bytes_sent <= 0) {
            return -1;
        }

        total_sent += bytes_sent;
    }

    return 0;
}

/* Recieves messages based on agreed socket retrieval and socket termination markers */
int recv_reply(int socket_fd, char *server_response, size_t response_size) {

    size_t total_received = 0;
    char buffer[256];

    if (response_size < 2) {
        return -1;
    }

    server_response[0] = '\0';

    while (1) {

        ssize_t bytes_received = recv(
            socket_fd,
            buffer,
            sizeof(buffer),
            0
        );

        if (bytes_received <= 0) {
            return -1;
        }

        if (total_received + (size_t)bytes_received >= response_size) {
            return -1;
        }

        memcpy(
            server_response + total_received,
            buffer,
            (size_t)bytes_received
        );

        total_received += (size_t)bytes_received;
        server_response[total_received] = '\0';

        /* Examine complete lines for the final response */
        char *line_start = server_response;
        char *newline;

        while ((newline = strchr(line_start, '\n')) != NULL) {

            size_t line_length = (size_t)(newline - line_start);

            if ((line_length == 2 &&
                 strncmp(line_start, "OK", 2) == 0) ||
                (line_length >= 4 &&
                 strncmp(line_start, "ERR ", 4) == 0)) {

                return 0;
            }

            line_start = newline + 1;
        }
    }
}

int main(){
    
    /* Implementation of UNIX domain socket client */
    char client_message[MAX_LINE + 2] = {0};
    char server_response[REPLY_MAX + 1];

    while (1) {


        /* Get command from terminal */
        if (!get_command(client_message, sizeof(client_message))) {
            break;
        }

        if (strcmp(client_message, "exit") == 0) {
            break;
        }
        size_t command_length = strlen(client_message);

        /* Format message according to standards set for messaging*/
        client_message[command_length] = '\n';
        client_message[command_length + 1] = '\0';

        /* Create a socket */
        int network_socket = socket(AF_LOCAL, SOCK_STREAM, 0);

        if (network_socket < 0) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        /* Specify an address for the socket + Setting up the address structure */
        struct sockaddr_un server_address = {0};

        /* Type of address we are working with */
        server_address.sun_family = AF_LOCAL;

        /* Specify the file path */ 
        strncpy(server_address.sun_path, SOCK_PATH, sizeof(server_address.sun_path) - 1);

        /* Connect to the daemon */
        int connection_status = connect(network_socket, (struct sockaddr *) &server_address, sizeof(server_address));

        if (connection_status == -1) {
            perror("connection");
            close(network_socket);
            continue;
        }

        /* Send message to daemon */
        int send_status = send_command(network_socket, client_message, command_length);

        if (send_status == -1) {
            perror("send");
            close(network_socket);
            continue;
        }

        /* Receive data from the daemon */
        int recv_status = recv_reply(network_socket, server_response, sizeof(server_response));

        /* Print out the server's response */
        if (recv_status == 0) {

            printf("%s", server_response);

        } else {

            perror("recv");
        }

        /* Close the socket */
        close(network_socket);
    }

    return 0;
}
