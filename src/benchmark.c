#include "benchmark.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static double elapsed_ms(const struct timespec *start, const struct timespec *end)
{
    time_t seconds = end->tv_sec - start->tv_sec;
    long nanoseconds = end->tv_nsec - start->tv_nsec;

    if (nanoseconds < 0) {
        seconds -= 1;
        nanoseconds += 1000000000L;
    }
    return (double)seconds * 1000.0 + (double)nanoseconds / 1000000.0;
}

static void describe_mismatch(const AnalysisResult *serial, const AnalysisResult *parallel,
                              char *detail, size_t detail_len)
{
    size_t column;

    if (serial->records != parallel->records) {
        snprintf(detail, detail_len, "record counts differ (%zu serial, %zu parallel)",
                 serial->records, parallel->records);
        return;
    }
    if (serial->incomplete_records != parallel->incomplete_records ||
        serial->missing_cells != parallel->missing_cells) {
        snprintf(detail, detail_len,
                 "quality totals differ (incomplete %zu vs %zu, missing cells %zu vs %zu)",
                 serial->incomplete_records, parallel->incomplete_records,
                 serial->missing_cells, parallel->missing_cells);
        return;
    }
    for (column = 0; column < serial->column_count; column++) {
        const ColumnStats *left = &serial->columns[column];
        const ColumnStats *right = &parallel->columns[column];
        if (left->valid_numeric != right->valid_numeric ||
            left->invalid != right->invalid ||
            left->missing != right->missing ||
            left->out_of_range != right->out_of_range) {
            snprintf(detail, detail_len, "column %zu counters differ", column);
            return;
        }
    }
    snprintf(detail, detail_len, "floating-point statistics differ beyond tolerance");
}

int benchmark_run(const Dataset *dataset, const Schema *schema, int threads,
                  BenchmarkResult *bench, AnalysisResult *out, ParallelInfo *info)
{
    AnalysisResult serial;
    AnalysisResult parallel;
    struct timespec start;
    struct timespec end;
    int status;

    if (!dataset || !schema || !bench || !out) {
        return DS_ERR_ARG;
    }
    memset(bench, 0, sizeof(*bench));
    memset(&serial, 0, sizeof(serial));
    memset(&parallel, 0, sizeof(parallel));

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        return DS_ERR_IO;
    }
    status = analyze_serial(dataset, schema, &serial);
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        analysis_free(&serial);
        return DS_ERR_IO;
    }
    if (status != DS_OK) {
        analysis_free(&serial);
        return status;
    }
    bench->serial_ms = elapsed_ms(&start, &end);

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        analysis_free(&serial);
        return DS_ERR_IO;
    }
    status = analyze_parallel(dataset, schema, threads, info, &parallel);
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        analysis_free(&serial);
        analysis_free(&parallel);
        return DS_ERR_IO;
    }
    if (status != DS_OK) {
        analysis_free(&serial);
        analysis_free(&parallel);
        return status;
    }
    bench->parallel_ms = elapsed_ms(&start, &end);

    bench->equivalent = analysis_equivalent(&serial, &parallel, DS_COMPARE_TOLERANCE);
    if (!bench->equivalent) {
        describe_mismatch(&serial, &parallel, bench->detail, sizeof bench->detail);
        *out = serial;
        memset(&serial, 0, sizeof(serial));
        analysis_free(&parallel);
    } else {
        snprintf(bench->detail, sizeof bench->detail,
                 "serial and parallel statistics match");
        *out = parallel;
        memset(&parallel, 0, sizeof(parallel));
        analysis_free(&serial);
    }

    /*
     * Sub-microsecond pairs are reported, but a speedup ratio would mostly
     * reflect timer noise. 0.001 ms is one microsecond.
     */
    if (bench->serial_ms <= 0.0 || bench->parallel_ms <= 0.0 ||
        (bench->serial_ms < 0.001 && bench->parallel_ms < 0.001)) {
        bench->speedup_valid = 0;
        bench->comparison = 0;
        bench->speedup = 0.0;
    } else {
        bench->speedup_valid = 1;
        bench->speedup = bench->serial_ms / bench->parallel_ms;
        if (bench->speedup > 1.01) {
            bench->comparison = 1;
        } else if (bench->speedup < 0.99) {
            bench->comparison = -1;
        } else {
            bench->comparison = 0;
        }
    }
    return DS_OK;
}
