#ifndef MASTERMIND_TCP_TRANSPORT_H
#define MASTERMIND_TCP_TRANSPORT_H

#include "protocol.h"
#include <stdint.h>

typedef struct {
    int listen_fd;
    uint16_t listen_port;
    int conn_fd;
    char read_buf[2048];
    size_t read_len;
} TcpManager;

/**
 * Initializes TCP listener on the specified port.
 * If port is 0, OS allocates an available ephemeral port and populates listen_port.
 * Returns 0 on success, -1 on failure.
 */
int tcp_init_listener(TcpManager *tcp, uint16_t port);

/**
 * Connects to peer at given IP and port.
 * Returns connected socket fd on success, -1 on failure.
 */
int tcp_connect_peer(const char *ip, uint16_t port);

/**
 * Sends a structured game message over TCP with length-prefixed framing.
 * Returns 0 on success, -1 on write error.
 */
int tcp_send_msg(int fd, MessageType type, const char *payload);

/**
 * Reads and unpacks a framed game message from the TCP stream.
 * Returns 1 if full message parsed, 0 if need more data (EAGAIN), -1 on disconnect/error.
 */
int tcp_recv_msg(int fd, char *buf, size_t *buf_len, GameMessage *out_msg);

/**
 * Closes connections and listener.
 */
void tcp_close(TcpManager *tcp);

#endif /* MASTERMIND_TCP_TRANSPORT_H */
