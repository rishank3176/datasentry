#ifndef DATASENTRY_CLI_H
#define DATASENTRY_CLI_H

#include "util.h"

#include <stddef.h>
#include <stdio.h>

typedef struct RangeSpec {
    char *column;
    double min_value;
    double max_value;
} RangeSpec;

typedef struct Options {
    char *dataset_path;
    int threads;
    int benchmark;
    int json;
    int help;
    RangeSpec *ranges;
    size_t range_count;
} Options;

void options_init(Options *options);
void options_free(Options *options);

/* On failure options may hold partial allocations. Always call options_free. */
int options_parse(int argc, char **argv, Options *options, char *err, size_t errlen);

void options_print_help(FILE *out);

#endif
