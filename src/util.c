#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *ds_strdup(const char *text)
{
    size_t length;
    char *copy;

    if (!text) {
        return NULL;
    }
    length = strlen(text);
    copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

char *ds_trim_dup(const char *text)
{
    const unsigned char *start;
    const unsigned char *end;
    size_t length;
    char *copy;

    if (!text) {
        return NULL;
    }
    start = (const unsigned char *)text;
    while (*start && isspace(*start)) {
        start++;
    }
    end = start + strlen((const char *)start);
    while (end > start && isspace(end[-1])) {
        end--;
    }
    length = (size_t)(end - start);
    copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static int is_finite_double(double value)
{
    return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

int ds_parse_finite(const char *text, double *out)
{
    const unsigned char *cursor;
    char *end;
    double value;

    if (!text) {
        return 0;
    }
    cursor = (const unsigned char *)text;
    while (*cursor && isspace(*cursor)) {
        cursor++;
    }
    if (*cursor == '\0') {
        return 0;
    }

    errno = 0;
    value = strtod((const char *)cursor, &end);
    if (end == (char *)cursor) {
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
    /* Overflow becomes HUGE_VAL with ERANGE. Underflow also sets ERANGE. */
    if (errno == ERANGE || !is_finite_double(value)) {
        return 0;
    }
    if (out) {
        *out = value;
    }
    return 1;
}

void ds_set_error(char *err, size_t errlen, const char *message)
{
    if (!err || errlen == 0) {
        return;
    }
    snprintf(err, errlen, "%s", message ? message : "");
}
