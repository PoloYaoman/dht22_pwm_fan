#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "types/index.h"

#define SERVER_PORT 4242
#define BUFFER_SIZE 1460

int main() {
    int sock;
    struct sockaddr_in server_addr;
    uint8_t buffer[BUFFER_SIZE];

    int on = 1;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sock);
        return EXIT_FAILURE;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return EXIT_FAILURE;
    }

    printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

    while (on) {
        printf("\nType 'help' to get a list of commands or 'exit' to quit.\n >> ");
        uint8_t sent_cmd[BUFFER_SIZE];
        fgets(sent_cmd, BUFFER_SIZE, stdin);

        if (strncmp(sent_cmd, "exit", 4) == 0) {
            on = 0;
            break;
        } else if (strncmp(sent_cmd, "help", 4) == 0) {
            printf("\nAvailable commands:\n");
            printf("help - Show this help message\n");
            printf("exit - Exit the program\n");
            printf("status - show system status\n");
            printf("setpwm <value> - set PWM value (0-100 or -1 for default control)\n\n");
            continue;
        } else {
            // Send request to server
            send(sock, sent_cmd, BUFFER_SIZE, MSG_CONFIRM);
            printf("\nRequest sent: %s\n", sent_cmd);
        }

        // Receive response from server
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(sock, buffer, BUFFER_SIZE-1, 0);
        if (bytes_received < 0) {
            perror("Receive failed");
            close(sock);
            return EXIT_FAILURE;
        } else {
            buffer[bytes_received] = '\0'; // Null-terminate the received data
            printf("Response from server: %s", buffer);
        }
    }

    // Close the socket
    close(sock);
    return EXIT_SUCCESS;
}