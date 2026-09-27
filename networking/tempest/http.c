#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>

#define REQUEST_TIMEOUT_SEC 10

static ssize_t send_all(int fd, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, buf + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

int http_connect(const char *host, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        return -1;
    }

    int sockfd = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        /* Configure 10-second send and receive timeouts */
        struct timeval tv;
        tv.tv_sec = REQUEST_TIMEOUT_SEC;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break; /* Successfully connected */
        }

        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(res);
    return sockfd;
}

int http_send_request(int sockfd, const char *host, const char *encoded_city) {
    char req_buf[2048];
    int req_len = snprintf(req_buf, sizeof(req_buf),
        "GET /%s?0T HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: curl/8.5.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        encoded_city, host);

    if (req_len < 0 || (size_t)req_len >= sizeof(req_buf)) {
        return -1;
    }

    if (send_all(sockfd, req_buf, (size_t)req_len) < 0) {
        return -1;
    }
    return 0;
}

void http_response_free(HttpResponse *resp) {
    if (!resp) return;
    free(resp->request_str);
    free(resp->raw_response);
    free(resp->headers);
    free(resp->body);
    memset(resp, 0, sizeof(*resp));
}
