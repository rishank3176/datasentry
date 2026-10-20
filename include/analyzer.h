#ifndef DATASENTRY_ANALYZER_H
#define DATASENTRY_ANALYZER_H

#include "csv.h"

#include <stddef.h>

#define DS_DEFAULT_THREADS 4
#define DS_MAX_THREADS 64

/* A column is numeric when numeric_parsed * 10 >= non_missing * 7 and numeric_parsed >= 1. */
#define DS_NUMERIC_THRESHOLD_NUMERATOR 7
#define DS_NUMERIC_THRESHOLD_DENOMINATOR 10

typedef enum ColumnKind {
    COL_TEXT = 0,
    COL_NUMERIC = 1
} ColumnKind;

typedef struct ColumnInfo {
    ColumnKind kind;
    size_t non_missing;
    size_t numeric_parsed;
} ColumnInfo;

/* Inclusive bounds. active is 0 when the user did not configure this column. */
typedef struct RangeRule {
    int active;
    double min_value;
    double max_value;
} RangeRule;

typedef struct Schema {
    ColumnInfo *columns;
    RangeRule *rules;
    size_t column_count;
} Schema;

typedef struct ColumnStats {
    size_t valid_numeric;
    double min_value;
    double max_value;
    double sum;
    size_t invalid;
    size_t out_of_range;
    size_t missing;
} ColumnStats;

/*
 * Statistics for one analysis pass over structurally valid records.
 * Workers fill a private AnalysisResult. The coordinator merges them.
 */
typedef struct AnalysisResult {
    size_t records;
    size_t incomplete_records;
    size_t missing_cells;
    ColumnStats *columns;
    size_t column_count;
} AnalysisResult;

typedef struct ParallelInfo {
    int requested_threads;
    int effective_workers;
    int threads_created;
    int planned_workers;
    int fallback;
    char mode[160];
} ParallelInfo;

/* Classifies columns. Range rules start inactive. */
int schema_build(const Dataset *dataset, Schema *schema, char *err, size_t errlen);

void schema_free(Schema *schema);

/*
 * Attaches an inclusive range to a detected numeric column.
 * Rejects unknown columns, non-numeric columns, duplicate rules,
 * non-finite bounds, and minimum values that are not strictly below maximum.
 */
int schema_apply_range(Schema *schema, const Dataset *dataset,
                       const char *column, double min_value, double max_value,
                       char *err, size_t errlen);

int analysis_init(AnalysisResult *result, size_t column_count);
void analysis_free(AnalysisResult *result);
void analysis_merge(AnalysisResult *dst, const AnalysisResult *src);

double column_mean(const ColumnStats *stats);

/* Same dataset, schema, and range rules as the parallel path. */
int analyze_serial(const Dataset *dataset, const Schema *schema, AnalysisResult *out);

/*
 * Splits valid records into disjoint ranges and analyzes them with POSIX threads.
 * Each worker writes only its own AnalysisResult. Results are merged after join.
 * If pthread_create fails, remaining ranges are analyzed on the calling thread
 * and ParallelInfo.fallback is set. The returned statistics still cover every
 * valid record.
 */
int analyze_parallel(const Dataset *dataset, const Schema *schema,
                     int requested_threads, ParallelInfo *info,
                     AnalysisResult *out);

/* Exact integer counters. Floating statistics must match within relative tolerance. */
int analysis_equivalent(const AnalysisResult *left, const AnalysisResult *right,
                        double tolerance);

/*
 * Checks internal consistency: record coverage, missing-cell totals,
 * and the partition of each numeric column into missing, invalid, and valid.
 */
int analysis_invariants(const Dataset *dataset, const Schema *schema,
                        const AnalysisResult *result, char *err, size_t errlen);

#endif
