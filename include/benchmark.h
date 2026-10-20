#ifndef DATASENTRY_BENCHMARK_H
#define DATASENTRY_BENCHMARK_H

#include "analyzer.h"

#define DS_COMPARE_TOLERANCE 1e-9

typedef struct BenchmarkResult {
    double serial_ms;
    double parallel_ms;
    double speedup;
    int speedup_valid;
    /* 1 if parallel was faster, -1 if slower, 0 if similar or not comparable. */
    int comparison;
    int equivalent;
    char detail[512];
} BenchmarkResult;

/*
 * Times serial and parallel analysis of an already parsed dataset.
 * Column detection is the caller's job and is not included.
 * Parallel timing includes thread creation, joining, and result merging.
 * On success, *out receives one copy of the statistics (parallel when the
 * two runs match, serial when they do not).
 */
int benchmark_run(const Dataset *dataset, const Schema *schema, int threads,
                  BenchmarkResult *bench, AnalysisResult *out, ParallelInfo *info);

#endif
