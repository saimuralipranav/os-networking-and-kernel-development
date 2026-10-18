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
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <ctype.h>

#define WTTR_HOST "wttr.is"
#define WTTR_PORT "80"
#define REQUEST_TIMEOUT_SEC 10

static int get_remaining_timeout_ms(struct timespec start_ts, int total_timeout_sec) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - start_ts.tv_sec) * 1000L +
                      (now.tv_nsec - start_ts.tv_nsec) / 1000000L;
    long remaining_ms = (long)total_timeout_sec * 1000L - elapsed_ms;
    return (remaining_ms > 0) ? (int)remaining_ms : 0;
}

static ssize_t send_all(int fd, const char *buf, size_t len, struct timespec start_ts, char *err_buf, size_t err_sz) {
    size_t total = 0;
    while (total < len) {
        int rem = get_remaining_timeout_ms(start_ts, REQUEST_TIMEOUT_SEC);
        if (rem <= 0) {
            snprintf(err_buf, err_sz, "request timed out");
            return -1;
        }

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, rem);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            snprintf(err_buf, err_sz, "send failed: %s", strerror(errno));
            return -1;
        }
        if (pr == 0) {
            snprintf(err_buf, err_sz, "request timed out");
            return -1;
        }

        ssize_t n = send(fd, buf + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            snprintf(err_buf, err_sz, "send failed: %s", strerror(errno));
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
        snprintf(resp->error_msg, sizeof(resp->error_msg), "failed to resolve host %s: %s", WTTR_HOST, gai_strerror(rc));
        return -1;
    }

    /* Start monotonic 10-second deadline before connect() per Doubt Q5 & Q15 */
    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    int sockfd = -1;
    int last_conn_error = 0;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        int rem = get_remaining_timeout_ms(start_ts, REQUEST_TIMEOUT_SEC);
        if (rem <= 0) {
            last_conn_error = ETIMEDOUT;
            break;
        }

        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) {
            last_conn_error = errno;
            continue;
        }

        /* Set non-blocking to bound connect() by remaining monotonic deadline */
        int flags = fcntl(sockfd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        }

        int conn_rc = connect(sockfd, p->ai_addr, p->ai_addrlen);
        if (conn_rc == 0) {
            break; /* Immediate connection */
        }

        if (conn_rc < 0 && (errno == EINPROGRESS || errno == EINTR)) {
            struct pollfd pfd;
            pfd.fd = sockfd;
            pfd.events = POLLOUT;
            pfd.revents = 0;

            int pr = poll(&pfd, 1, rem);
            if (pr > 0 && (pfd.revents & (POLLOUT | POLLERR | POLLHUP))) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0) {
                    if (so_error == 0) {
                        break; /* Connected successfully */
                    }
                    last_conn_error = so_error;
                } else {
                    last_conn_error = errno ? errno : ECONNREFUSED;
                }
            } else if (pr == 0) {
                last_conn_error = ETIMEDOUT;
            } else {
                last_conn_error = errno;
            }
        } else {
            last_conn_error = errno;
        }

        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(res);

    if (sockfd < 0) {
        if (get_remaining_timeout_ms(start_ts, REQUEST_TIMEOUT_SEC) <= 0 || last_conn_error == ETIMEDOUT) {
            snprintf(resp->error_msg, sizeof(resp->error_msg), "request timed out");
        } else {
            snprintf(resp->error_msg, sizeof(resp->error_msg), "connection failed: %s", strerror(last_conn_error ? last_conn_error : ECONNREFUSED));
        }
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
        snprintf(resp->error_msg, sizeof(resp->error_msg), "request buffer overflow");
        close(sockfd);
        return -1;
    }

    resp->request_str = strdup(req_buf);

    /* Send request with short write handling bounded by monotonic deadline */
    if (send_all(sockfd, req_buf, (size_t)req_len, start_ts, resp->error_msg, sizeof(resp->error_msg)) < 0) {
        close(sockfd);
        return -1;
    }

    /* Read response with short read handling until connection closes */
    size_t cap = 8192;
    size_t total_read = 0;
    char *recv_buf = malloc(cap);
    if (!recv_buf) {
        snprintf(resp->error_msg, sizeof(resp->error_msg), "out of memory");
        close(sockfd);
        return -1;
    }

    while (1) {
        int rem = get_remaining_timeout_ms(start_ts, REQUEST_TIMEOUT_SEC);
        if (rem <= 0) {
            snprintf(resp->error_msg, sizeof(resp->error_msg), "request timed out");
            free(recv_buf);
            close(sockfd);
            return -1;
        }

        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, rem);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            snprintf(resp->error_msg, sizeof(resp->error_msg), "recv failed: %s", strerror(errno));
            free(recv_buf);
            close(sockfd);
            return -1;
        }
        if (pr == 0) {
            snprintf(resp->error_msg, sizeof(resp->error_msg), "request timed out");
            free(recv_buf);
            close(sockfd);
            return -1;
        }

        if (total_read + 2048 >= cap) {
            cap *= 2;
            char *new_buf = realloc(recv_buf, cap);
            if (!new_buf) {
                snprintf(resp->error_msg, sizeof(resp->error_msg), "out of memory");
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
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            snprintf(resp->error_msg, sizeof(resp->error_msg), "recv failed: %s", strerror(errno));
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
            snprintf(resp->error_msg, sizeof(resp->error_msg), "malformed HTTP response");
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
