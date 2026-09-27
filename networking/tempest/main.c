#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "url.h"
#include "http.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "tempest: invalid location\n");
        return 1;
    }

    if (argc > 2) {
        fprintf(stderr, "tempest: too many arguments\n");
        return 1;
    }

    const char *city_arg = argv[1];
    if (city_arg[0] == '\0' || strcmp(city_arg, "--raw") == 0) {
        fprintf(stderr, "tempest: invalid location\n");
        return 1;
    }

    char *encoded_city = url_encode(city_arg);
    if (!encoded_city) {
        fprintf(stderr, "tempest: invalid location\n");
        return 1;
    }

    HttpResponse resp;
    int rc = http_get_weather(encoded_city, &resp);
    free(encoded_city);

    if (rc != 0 || !resp.is_valid_location) {
        http_response_free(&resp);
        fprintf(stderr, "tempest: invalid location\n");
        return 1;
    }

    /* Normal mode: print only the weather body without headers */
    if (resp.body) {
        fputs(resp.body, stdout);
    }

    http_response_free(&resp);
    return 0;
}
