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
#include <ctype.h>

#define WTTR_HOST "wttr.is"
#define WTTR_PORT "80"
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

static char *decode_chunked_body(const char *body_start, size_t body_len, size_t *out_len) {
    char *decoded = malloc(body_len + 1);
    if (!decoded) {
        return NULL;
    }

    const char *p = body_start;
    const char *end = body_start + body_len;
    size_t decoded_len = 0;

    while (p < end) {
        /* Read hex chunk size */
        char *endptr = NULL;
        unsigned long chunk_sz = strtoul(p, &endptr, 16);
        if (endptr == p) {
            /* Parsing error */
            break;
        }

        /* Skip past chunk size and any chunk extensions until CRLF */
        const char *crlf = strstr(p, "\r\n");
        if (!crlf) {
            break;
        }
        p = crlf + 2;

        if (chunk_sz == 0) {
            /* Last chunk (0\r\n\r\n) */
            break;
        }

        if (p + chunk_sz > end) {
            /* Incomplete chunk */
            break;
        }

        memcpy(decoded + decoded_len, p, chunk_sz);
        decoded_len += chunk_sz;
        p += chunk_sz;

        /* Skip trailing CRLF of the chunk */
        if (p + 2 <= end && p[0] == '\r' && p[1] == '\n') {
            p += 2;
        }
    }

    decoded[decoded_len] = '\0';
    if (out_len) {
        *out_len = decoded_len;
    }
    return decoded;
}

int http_get_weather(const char *encoded_city, HttpResponse *resp) {
    if (!encoded_city || !resp) {
        return -1;
    }
    memset(resp, 0, sizeof(*resp));

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(WTTR_HOST, WTTR_PORT, &hints, &res);
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

    if (sockfd < 0) {
        return -1;
    }

    /* Build HTTP/1.1 GET request */
    char req_buf[2048];
    int req_len = snprintf(req_buf, sizeof(req_buf),
        "GET /%s?0T HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: curl/8.5.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        encoded_city, WTTR_HOST);

    if (req_len < 0 || (size_t)req_len >= sizeof(req_buf)) {
        close(sockfd);
        return -1;
    }

    resp->request_str = strdup(req_buf);

    /* Send request with short write handling */
    if (send_all(sockfd, req_buf, (size_t)req_len) < 0) {
        close(sockfd);
        return -1;
    }

    /* Read response with short read handling until connection closes */
    size_t cap = 8192;
    size_t total_read = 0;
    char *recv_buf = malloc(cap);
    if (!recv_buf) {
        close(sockfd);
        return -1;
    }

    while (1) {
        if (total_read + 2048 >= cap) {
            cap *= 2;
            char *new_buf = realloc(recv_buf, cap);
            if (!new_buf) {
                free(recv_buf);
                close(sockfd);
                return -1;
            }
            recv_buf = new_buf;
        }

        ssize_t n = recv(sockfd, recv_buf + total_read, cap - total_read - 1, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            /* Timeout or socket error */
            free(recv_buf);
            close(sockfd);
            return -1;
        }
        if (n == 0) {
            /* Server closed connection (Connection: close) */
            break;
        }
        total_read += (size_t)n;
    }
    close(sockfd);

    recv_buf[total_read] = '\0';
    resp->raw_response = recv_buf;
    resp->raw_response_len = total_read;

    /* Locate boundary between headers and body */
    char *sep = strstr(recv_buf, "\r\n\r\n");
    size_t header_len = 0;
    const char *body_start = NULL;
    size_t body_len = 0;

    if (sep) {
        header_len = (size_t)(sep - recv_buf);
        body_start = sep + 4;
        body_len = total_read - (header_len + 4);
    } else {
        /* Fallback for \n\n */
        sep = strstr(recv_buf, "\n\n");
        if (sep) {
            header_len = (size_t)(sep - recv_buf);
            body_start = sep + 2;
            body_len = total_read - (header_len + 2);
        } else {
            /* No valid HTTP header separator found */
            return -1;
        }
    }

    resp->headers = malloc(header_len + 1);
    if (resp->headers) {
        memcpy(resp->headers, recv_buf, header_len);
        resp->headers[header_len] = '\0';
    }

    /* Parse status code from status line */
    if (sscanf(recv_buf, "HTTP/%*s %d", &resp->status_code) != 1) {
        resp->status_code = 0;
    }

    /* Check if response is chunked */
    int is_chunked = 0;
    if (resp->headers) {
        char *h_lower = strdup(resp->headers);
        if (h_lower) {
            for (size_t i = 0; h_lower[i]; i++) {
                h_lower[i] = (char)tolower((unsigned char)h_lower[i]);
            }
            if (strstr(h_lower, "transfer-encoding: chunked") || strstr(h_lower, "transfer-encoding:chunked")) {
                is_chunked = 1;
            }
            free(h_lower);
        }
    }

    if (is_chunked) {
        resp->body = decode_chunked_body(body_start, body_len, NULL);
    } else {
        resp->body = malloc(body_len + 1);
        if (resp->body) {
            memcpy(resp->body, body_start, body_len);
            resp->body[body_len] = '\0';
        }
    }

    /* Validate location correctness:
     * - Must be HTTP 200 OK
     * - Body must not contain wttr.is failure messages
     */
    if (resp->status_code == 200 && resp->body) {
        if (strstr(resp->body, "location not found") ||
            strstr(resp->body, "Unknown location") ||
            strstr(resp->body, "upstream error") ||
            strstr(resp->body, "NOT FOUND")) {
            resp->is_valid_location = 0;
        } else {
            resp->is_valid_location = 1;
        }
    } else {
        resp->is_valid_location = 0;
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
