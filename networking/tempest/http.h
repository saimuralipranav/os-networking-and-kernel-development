#ifndef TEMPEST_HTTP_H
#define TEMPEST_HTTP_H

#include <stddef.h>

typedef struct {
    char *request_str;       /* Full raw request sent */
    char *raw_response;      /* Full raw response received */
    size_t raw_response_len; /* Total bytes in raw_response */
    char *headers;           /* Response headers including status line */
    char *body;              /* Parsed and decoded weather report body */
    int status_code;         /* HTTP status code (e.g. 200, 404, 500) */
    int is_valid_location;   /* 1 if location found and report valid, 0 otherwise */
} HttpResponse;

int http_get_weather(const char *encoded_city, HttpResponse *resp);
void http_response_free(HttpResponse *resp);

#endif /* TEMPEST_HTTP_H */
