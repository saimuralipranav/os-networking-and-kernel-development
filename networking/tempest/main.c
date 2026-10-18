#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "url.h"
#include "http.h"

static void print_prefixed_lines(const char *prefix, const char *str) {
    if (!str) return;
    const char *line_start = str;
    while (*line_start) {
        const char *line_end = strchr(line_start, '\n');
        printf("%s", prefix);
        if (line_end) {
            /* Print up to newline, stripping possible \r */
            size_t len = (size_t)(line_end - line_start);
            if (len > 0 && line_start[len - 1] == '\r') {
                len--;
            }
            fwrite(line_start, 1, len, stdout);
            putchar('\n');
            line_start = line_end + 1;
        } else {
            /* Last line without trailing newline */
            size_t len = strlen(line_start);
            if (len > 0 && line_start[len - 1] == '\r') {
                len--;
            }
            fwrite(line_start, 1, len, stdout);
            putchar('\n');
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "tempest: invalid location\n");
        return 1;
    }

    int raw_mode = 0;
    const char *city_arg = argv[1];

    if (argc == 2) {
        raw_mode = 0;
    } else if (argc == 3) {
        if (strcmp(argv[2], "--raw") == 0) {
            raw_mode = 1;
        } else {
            fprintf(stderr, "tempest: too many arguments\n");
            return 1;
        }
    } else {
        fprintf(stderr, "tempest: too many arguments\n");
        return 1;
    }

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
    memset(&resp, 0, sizeof(resp));
    int rc = http_get_weather(encoded_city, &resp);
    free(encoded_city);

    if (raw_mode) {
        /* Print request with '> ' prefix */
        if (resp.request_str) {
            print_prefixed_lines("> ", resp.request_str);
        }

        /* Print response headers with '< ' prefix */
        if (resp.headers) {
            print_prefixed_lines("< ", resp.headers);
            /* Print the blank line between headers and body with '< ' prefix */
            printf("<\n");
        }

        /* Print raw weather body directly */
        if (resp.body) {
            fputs(resp.body, stdout);
        }

        if (rc != 0) {
            if (resp.error_msg[0] != '\0') {
                fprintf(stderr, "tempest: %s\n", resp.error_msg);
            } else {
                fprintf(stderr, "tempest: network error\n");
            }
        }

        int exit_code = (rc != 0 || !resp.is_valid_location) ? 1 : 0;
        http_response_free(&resp);
        return exit_code;
    }

    if (rc != 0) {
        if (resp.error_msg[0] != '\0') {
            fprintf(stderr, "tempest: %s\n", resp.error_msg);
        } else {
            fprintf(stderr, "tempest: network error\n");
        }
        http_response_free(&resp);
        return 1;
    }

    if (!resp.is_valid_location) {
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
