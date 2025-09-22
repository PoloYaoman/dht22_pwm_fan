#ifndef TCP_CLIENT_TYPES_H
#define TCP_CLIENT_TYPES_H

#include <stdint.h>

#define SERVER_IP "0.0.0.0"  // Replace with the actual server IP address
#define SERVER_PORT 4242

typedef struct {
    uint8_t *data;
    size_t length;
} tcp_message_t;

void send_message(int socket, tcp_message_t *message);
tcp_message_t receive_message(int socket);

#endif // TCP_CLIENT_TYPES_H