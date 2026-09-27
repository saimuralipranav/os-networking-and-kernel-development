#include "tcp_transport.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int tcp_init_listener(TcpManager *tcp, uint16_t port) {
    if (!tcp) return -1;
    memset(tcp, 0, sizeof(*tcp));
    tcp->conn_fd = -1;

    tcp->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp->listen_fd < 0) {
        return -1;
    }

    int opt_reuse = 1;
    setsockopt(tcp->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse, sizeof(opt_reuse));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(tcp->listen_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        close(tcp->listen_fd);
        tcp->listen_fd = -1;
        return -1;
    }

    if (listen(tcp->listen_fd, 5) < 0) {
        close(tcp->listen_fd);
        tcp->listen_fd = -1;
        return -1;
    }

    /* Retrieve dynamically bound port if 0 was passed */
    struct sockaddr_in actual_addr;
    socklen_t actual_len = sizeof(actual_addr);
    if (getsockname(tcp->listen_fd, (struct sockaddr *)&actual_addr, &actual_len) == 0) {
        tcp->listen_port = ntohs(actual_addr.sin_port);
    } else {
        tcp->listen_port = port;
    }

    /* Set listener to non-blocking */
    int flags = fcntl(tcp->listen_fd, F_GETFL, 0);
    fcntl(tcp->listen_fd, F_SETFL, flags | O_NONBLOCK);

    logger_log("TCP listener active on port %u", tcp->listen_port);
    return 0;
}

int tcp_connect_peer(const char *ip, uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &dest.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    /* Connect to peer */
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        close(sock);
        return -1;
    }

    /* Set connected socket to non-blocking */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    logger_log("TCP connected to %s:%u", ip, port);
    return sock;
}

int tcp_send_msg(int fd, MessageType type, const char *payload) {
    if (fd < 0) return -1;

    size_t payload_len = payload ? strlen(payload) : 0;
    if (payload_len > 1024) payload_len = 1024;

    /* Frame format: [1 byte type][2 bytes payload_len in network order][payload] */
    uint8_t header[3];
    header[0] = (uint8_t)type;
    header[1] = (uint8_t)((payload_len >> 8) & 0xFF);
    header[2] = (uint8_t)(payload_len & 0xFF);

    /* Send header */
    size_t total = 0;
    while (total < 3) {
        ssize_t n = send(fd, header + total, 3 - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
    }

    /* Send payload */
    if (payload_len > 0) {
        total = 0;
        while (total < payload_len) {
            ssize_t n = send(fd, payload + total, payload_len - total, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            if (n == 0) return -1;
            total += (size_t)n;
        }
    }

    logger_log("TCP sent message type=%d, len=%zu", (int)type, payload_len);
    return 0;
}

int tcp_recv_msg(int fd, char *buf, size_t *buf_len, GameMessage *out_msg) {
    if (fd < 0 || !buf || !buf_len || !out_msg) return -1;

    /* Read available bytes into buffer */
    while (*buf_len < 2048) {
        ssize_t n = recv(fd, buf + *buf_len, 2048 - *buf_len, 0);
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                break;
            }
            return -1; /* Socket read error */
        }
        if (n == 0) {
            return -1; /* Remote peer closed connection */
        }
        *buf_len += (size_t)n;
    }

    /* Frame requires at least 3 bytes for header */
    if (*buf_len < 3) {
        return 0;
    }

    MessageType type = (MessageType)(unsigned char)buf[0];
    size_t payload_len = (((size_t)(unsigned char)buf[1]) << 8) | ((size_t)(unsigned char)buf[2]);
    size_t full_frame_len = 3 + payload_len;

    if (*buf_len < full_frame_len) {
        return 0; /* Incomplete frame, wait for more bytes */
    }

    /* Extract message */
    out_msg->type = type;
    if (payload_len > sizeof(out_msg->text) - 1) {
        payload_len = sizeof(out_msg->text) - 1;
    }
    memcpy(out_msg->text, buf + 3, payload_len);
    out_msg->text[payload_len] = '\0';

    /* Shift remaining bytes in buffer */
    size_t remaining = *buf_len - full_frame_len;
    if (remaining > 0) {
        memmove(buf, buf + full_frame_len, remaining);
    }
    *buf_len = remaining;

    logger_log("TCP received message type=%d, len=%zu", (int)out_msg->type, payload_len);
    return 1;
}

void tcp_close(TcpManager *tcp) {
    if (!tcp) return;
    if (tcp->conn_fd >= 0) {
        close(tcp->conn_fd);
        tcp->conn_fd = -1;
    }
    if (tcp->listen_fd >= 0) {
        close(tcp->listen_fd);
        tcp->listen_fd = -1;
    }
}
