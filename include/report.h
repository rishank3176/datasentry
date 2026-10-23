#ifndef DATASENTRY_REPORT_H
#define DATASENTRY_REPORT_H

#include "benchmark.h"
#include "cli.h"

#include <stdio.h>

typedef struct Report {
    const char *path;
    const Dataset *dataset;
    const Schema *schema;
    const AnalysisResult *analysis;
    const ParallelInfo *parallel;
    const BenchmarkResult *benchmark; /* NULL when --benchmark was not requested. */
    const char **warnings;
    size_t warning_count;
} Report;

void report_print(FILE *out, const Report *report);
void report_print_json(FILE *out, const Report *report);

#endif
