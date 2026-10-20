#ifndef DATASENTRY_UTIL_H
#define DATASENTRY_UTIL_H

#include <stddef.h>

/* Status codes shared by parsing, analysis, and the command-line interface. */
typedef enum DsStatus {
    DS_OK = 0,
    DS_ERR_IO = 1,
    DS_ERR_FORMAT = 2,
    DS_ERR_NOMEM = 3,
    DS_ERR_ARG = 4,
    DS_ERR_THREAD = 5
} DsStatus;

/* Copies text. Returns NULL if text is NULL or allocation fails. */
char *ds_strdup(const char *text);

/*
 * Copies text without leading or trailing whitespace.
 * Returns an empty string when text is only whitespace.
 * Returns NULL on allocation failure.
 */
char *ds_trim_dup(const char *text);

/*
 * Parses a complete finite decimal number.
 * Leading and trailing whitespace is ignored. Trailing junk, NaN,
 * infinities, and values that overflow or underflow strtod are rejected.
 * Returns 1 on success and 0 otherwise. out may be NULL.
 */
int ds_parse_finite(const char *text, double *out);

void ds_set_error(char *err, size_t errlen, const char *message);

#endif
