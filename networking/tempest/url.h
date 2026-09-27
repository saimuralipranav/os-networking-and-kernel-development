#ifndef TEMPEST_URL_H
#define TEMPEST_URL_H

#include <stddef.h>

/**
 * URL-encodes a string according to RFC 3986.
 * Returns a dynamically allocated string that the caller must free.
 * Returns NULL on memory allocation failure.
 */
char *url_encode(const char *str);

#endif /* TEMPEST_URL_H */
