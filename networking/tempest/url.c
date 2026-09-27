#include "url.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_unreserved(unsigned char c) {
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
        return 1;
    }
    return 0;
}

char *url_encode(const char *str) {
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str);
    /* In the worst case, every byte expands to %XX (3 bytes) + null terminator */
    size_t max_len = len * 3 + 1;
    char *encoded = malloc(max_len);
    if (!encoded) {
        return NULL;
    }

    char *dst = encoded;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (is_unreserved(c)) {
            *dst++ = (char)c;
        } else {
            snprintf(dst, 4, "%%%02X", c);
            dst += 3;
        }
    }
    *dst = '\0';
    return encoded;
}
