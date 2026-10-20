#include "analyzer.h"

#include <float.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cell_is_missing(const char *text)
{
    const unsigned char *cursor;

    if (!text) {
        return 1;
    }
    cursor = (const unsigned char *)text;
    while (*cursor) {
        if (*cursor != ' ' && *cursor != '\t' && *cursor != '\v' &&
            *cursor != '\f' && *cursor != '\r') {
            return 0;
        }
        cursor++;
    }
    return 1;
}

/*
 * numeric / non_missing >= 7/10, without dividing.
 * A quotient of 1 or more is always above the threshold.
 */
static int meets_numeric_threshold(size_t numeric, size_t non_missing)
{
    size_t quotient;
    size_t remainder;

    if (numeric == 0 || non_missing == 0) {
        return 0;
    }
    quotient = numeric / non_missing;
    if (quotient >= 1) {
        return 1;
    }
    remainder = numeric % non_missing;
    return (unsigned long long)remainder * DS_NUMERIC_THRESHOLD_DENOMINATOR >=
           (unsigned long long)non_missing * DS_NUMERIC_THRESHOLD_NUMERATOR;
}

static void observe_number(ColumnStats *stats, double value, const RangeRule *rule)
{
    if (stats->valid_numeric == 0) {
        stats->min_value = value;
        stats->max_value = value;
    } else {
        if (value < stats->min_value) {
            stats->min_value = value;
        }
        if (value > stats->max_value) {
            stats->max_value = value;
        }
    }
    stats->sum += value;
    stats->valid_numeric++;
    if (rule && rule->active && (value < rule->min_value || value > rule->max_value)) {
        stats->out_of_range++;
    }
}

static void analyze_range(const Dataset *dataset, const Schema *schema,
                          size_t start, size_t end, AnalysisResult *out)
{
    size_t row;
    size_t column;

    for (row = start; row < end; row++) {
        int row_missing = 0;
        for (column = 0; column < dataset->column_count; column++) {
            const char *cell = dataset->rows[row][column];
            ColumnStats *stats = &out->columns[column];
            double value;

            if (cell_is_missing(cell)) {
                stats->missing++;
                out->missing_cells++;
                row_missing = 1;
                continue;
            }
            if (schema->columns[column].kind != COL_NUMERIC) {
                continue;
            }
            if (!ds_parse_finite(cell, &value)) {
                stats->invalid++;
                continue;
            }
            observe_number(stats, value, &schema->rules[column]);
        }
        out->records++;
        if (row_missing) {
            out->incomplete_records++;
        }
    }
}

typedef struct WorkerArgs {
    const Dataset *dataset;
    const Schema *schema;
    size_t start;
    size_t end;
    AnalysisResult *result;
} WorkerArgs;

static void *worker_run(void *argument)
{
    WorkerArgs *worker = argument;
    analyze_range(worker->dataset, worker->schema, worker->start, worker->end,
                  worker->result);
    return NULL;
}

int schema_build(const Dataset *dataset, Schema *schema, char *err, size_t errlen)
{
    size_t column;
    size_t row;

    if (!dataset || !schema) {
        ds_set_error(err, errlen, "missing dataset");
        return DS_ERR_ARG;
    }
    memset(schema, 0, sizeof(*schema));
    schema->column_count = dataset->column_count;
    schema->columns = calloc(dataset->column_count, sizeof(*schema->columns));
    schema->rules = calloc(dataset->column_count, sizeof(*schema->rules));
    if (!schema->columns || !schema->rules) {
        schema_free(schema);
        ds_set_error(err, errlen, "out of memory while classifying columns");
        return DS_ERR_NOMEM;
    }

    for (column = 0; column < dataset->column_count; column++) {
        size_t non_missing = 0;
        size_t numeric = 0;
        for (row = 0; row < dataset->row_count; row++) {
            if (cell_is_missing(dataset->rows[row][column])) {
                continue;
            }
            non_missing++;
            if (ds_parse_finite(dataset->rows[row][column], NULL)) {
                numeric++;
            }
        }
        schema->columns[column].non_missing = non_missing;
        schema->columns[column].numeric_parsed = numeric;
        schema->columns[column].kind =
            meets_numeric_threshold(numeric, non_missing) ? COL_NUMERIC : COL_TEXT;
    }
    return DS_OK;
}

void schema_free(Schema *schema)
{
    if (!schema) {
        return;
    }
    free(schema->columns);
    free(schema->rules);
    schema->columns = NULL;
    schema->rules = NULL;
    schema->column_count = 0;
}

int schema_apply_range(Schema *schema, const Dataset *dataset,
                       const char *column, double min_value, double max_value,
                       char *err, size_t errlen)
{
    size_t index;

    if (!schema || !dataset || !column) {
        ds_set_error(err, errlen, "missing range rule");
        return DS_ERR_ARG;
    }
    /* NaN compares unequal with itself. Infinities fall outside the finite range. */
    if (!(min_value == min_value) || !(max_value == max_value) ||
        min_value > DBL_MAX || max_value > DBL_MAX ||
        min_value < -DBL_MAX || max_value < -DBL_MAX) {
        ds_set_error(err, errlen, "range bounds must be finite");
        return DS_ERR_ARG;
    }
    if (!(min_value < max_value)) {
        snprintf(err, errlen, "invalid range for '%s' (minimum must be less than maximum)",
                 column);
        return DS_ERR_ARG;
    }

    for (index = 0; index < dataset->column_count; index++) {
        if (strcmp(dataset->headers[index], column) == 0) {
            if (schema->columns[index].kind != COL_NUMERIC) {
                snprintf(err, errlen,
                         "range column '%s' is not numeric "
                         "(fewer than 70%% of its non-missing values are finite numbers)",
                         column);
                return DS_ERR_ARG;
            }
            if (schema->rules[index].active) {
                snprintf(err, errlen, "duplicate range rule for column '%s'", column);
                return DS_ERR_ARG;
            }
            schema->rules[index].active = 1;
            schema->rules[index].min_value = min_value;
            schema->rules[index].max_value = max_value;
            return DS_OK;
        }
    }

    snprintf(err, errlen, "range column '%s' was not found", column);
    return DS_ERR_ARG;
}

int analysis_init(AnalysisResult *result, size_t column_count)
{
    if (!result) {
        return DS_ERR_ARG;
    }
    memset(result, 0, sizeof(*result));
    result->column_count = column_count;
    result->columns = calloc(column_count ? column_count : 1, sizeof(*result->columns));
    if (!result->columns) {
        return DS_ERR_NOMEM;
    }
    return DS_OK;
}

void analysis_free(AnalysisResult *result)
{
    if (!result) {
        return;
    }
    free(result->columns);
    result->columns = NULL;
    result->column_count = 0;
    result->records = 0;
    result->incomplete_records = 0;
    result->missing_cells = 0;
}

void analysis_merge(AnalysisResult *dst, const AnalysisResult *src)
{
    size_t column;

    if (!dst || !src) {
        return;
    }
    dst->records += src->records;
    dst->incomplete_records += src->incomplete_records;
    dst->missing_cells += src->missing_cells;
    for (column = 0; column < dst->column_count && column < src->column_count; column++) {
        ColumnStats *destination = &dst->columns[column];
        const ColumnStats *source = &src->columns[column];

        if (source->valid_numeric > 0) {
            if (destination->valid_numeric == 0) {
                destination->min_value = source->min_value;
                destination->max_value = source->max_value;
            } else {
                if (source->min_value < destination->min_value) {
                    destination->min_value = source->min_value;
                }
                if (source->max_value > destination->max_value) {
                    destination->max_value = source->max_value;
                }
            }
            destination->sum += source->sum;
            destination->valid_numeric += source->valid_numeric;
        }
        destination->invalid += source->invalid;
        destination->out_of_range += source->out_of_range;
        destination->missing += source->missing;
    }
}

double column_mean(const ColumnStats *stats)
{
    if (!stats || stats->valid_numeric == 0) {
        return 0.0;
    }
    return stats->sum / (double)stats->valid_numeric;
}

int analyze_serial(const Dataset *dataset, const Schema *schema, AnalysisResult *out)
{
    int status;

    if (!dataset || !schema || !out) {
        return DS_ERR_ARG;
    }
    status = analysis_init(out, dataset->column_count);
    if (status != DS_OK) {
        return status;
    }
    analyze_range(dataset, schema, 0, dataset->row_count, out);
    return DS_OK;
}

static void fill_mode(ParallelInfo *info, int requested, int planned, int created, int fallback)
{
    info->requested_threads = requested;
    info->planned_workers = planned;
    info->threads_created = created;
    info->effective_workers = created;
    info->fallback = fallback;
    if (planned == 0) {
        snprintf(info->mode, sizeof info->mode, "serial (no valid records)");
    } else if (fallback && created == 0) {
        snprintf(info->mode, sizeof info->mode,
                 "serial fallback (could not create worker threads)");
    } else if (fallback) {
        snprintf(info->mode, sizeof info->mode,
                 "mixed fallback (%d of %d workers created; remainder analyzed serially)",
                 created, planned);
    } else if (planned < requested) {
        snprintf(info->mode, sizeof info->mode,
                 "multithreaded (%d workers; limited by the number of valid records)",
                 planned);
    } else {
        snprintf(info->mode, sizeof info->mode, "multithreaded (%d workers)", planned);
    }
}

int analyze_parallel(const Dataset *dataset, const Schema *schema,
                     int requested_threads, ParallelInfo *info, AnalysisResult *out)
{
    size_t worker_count;
    size_t base;
    size_t remainder;
    size_t cursor;
    size_t index;
    size_t created = 0;
    int fallback = 0;
    int join_failed = 0;
    AnalysisResult *partials = NULL;
    WorkerArgs *args = NULL;
    pthread_t *threads = NULL;
    int status = DS_OK;

    if (!dataset || !schema || !out) {
        return DS_ERR_ARG;
    }
    if (requested_threads < 1 || requested_threads > DS_MAX_THREADS) {
        return DS_ERR_ARG;
    }
    status = analysis_init(out, dataset->column_count);
    if (status != DS_OK) {
        return status;
    }
    if (dataset->row_count == 0) {
        if (info) {
            fill_mode(info, requested_threads, 0, 0, 0);
        }
        return DS_OK;
    }

    worker_count = (size_t)requested_threads;
    if (worker_count > dataset->row_count) {
        worker_count = dataset->row_count;
    }

    partials = calloc(worker_count, sizeof(*partials));
    args = calloc(worker_count, sizeof(*args));
    threads = calloc(worker_count, sizeof(*threads));
    if (!partials || !args || !threads) {
        status = DS_ERR_NOMEM;
        goto cleanup;
    }
    for (index = 0; index < worker_count; index++) {
        if (analysis_init(&partials[index], dataset->column_count) != DS_OK) {
            status = DS_ERR_NOMEM;
            goto cleanup;
        }
    }

    /*
     * First `remainder` workers receive one extra record so the ranges stay
     * contiguous, non-overlapping, and complete.
     */
    base = dataset->row_count / worker_count;
    remainder = dataset->row_count % worker_count;
    cursor = 0;
    for (index = 0; index < worker_count; index++) {
        size_t count = base + (index < remainder ? 1 : 0);
        args[index].dataset = dataset;
        args[index].schema = schema;
        args[index].start = cursor;
        args[index].end = cursor + count;
        args[index].result = &partials[index];
        cursor += count;
    }

    for (index = 0; index < worker_count; index++) {
        int create_status = pthread_create(&threads[index], NULL, worker_run, &args[index]);
        if (create_status != 0) {
            size_t serial;
            fallback = 1;
            for (serial = index; serial < worker_count; serial++) {
                analyze_range(dataset, schema, args[serial].start, args[serial].end,
                              &partials[serial]);
            }
            break;
        }
        created++;
    }

    for (index = 0; index < created; index++) {
        if (pthread_join(threads[index], NULL) != 0) {
            join_failed = 1;
        }
    }
    if (join_failed) {
        status = DS_ERR_THREAD;
        goto cleanup;
    }

    for (index = 0; index < worker_count; index++) {
        analysis_merge(out, &partials[index]);
    }
    if (info) {
        fill_mode(info, requested_threads, (int)worker_count, (int)created, fallback);
    }
    status = DS_OK;

cleanup:
    if (partials) {
        for (index = 0; index < worker_count; index++) {
            analysis_free(&partials[index]);
        }
    }
    free(partials);
    free(args);
    free(threads);
    if (status != DS_OK) {
        analysis_free(out);
    }
    return status;
}

static int doubles_close(double left, double right, double tolerance)
{
    double difference = left - right;
    double scale;
    if (difference < 0) {
        difference = -difference;
    }
    scale = left < 0 ? -left : left;
    if ((right < 0 ? -right : right) > scale) {
        scale = right < 0 ? -right : right;
    }
    if (scale < 1.0) {
        scale = 1.0;
    }
    return difference <= tolerance * scale;
}

int analysis_equivalent(const AnalysisResult *left, const AnalysisResult *right,
                        double tolerance)
{
    size_t column;

    if (!left || !right) {
        return 0;
    }
    if (left->column_count != right->column_count ||
        left->records != right->records ||
        left->incomplete_records != right->incomplete_records ||
        left->missing_cells != right->missing_cells) {
        return 0;
    }
    for (column = 0; column < left->column_count; column++) {
        const ColumnStats *a = &left->columns[column];
        const ColumnStats *b = &right->columns[column];
        if (a->valid_numeric != b->valid_numeric ||
            a->invalid != b->invalid ||
            a->out_of_range != b->out_of_range ||
            a->missing != b->missing) {
            return 0;
        }
        if (a->valid_numeric > 0) {
            if (!doubles_close(a->min_value, b->min_value, tolerance) ||
                !doubles_close(a->max_value, b->max_value, tolerance) ||
                !doubles_close(a->sum, b->sum, tolerance) ||
                !doubles_close(column_mean(a), column_mean(b), tolerance)) {
                return 0;
            }
        }
    }
    return 1;
}

int analysis_invariants(const Dataset *dataset, const Schema *schema,
                        const AnalysisResult *result, char *err, size_t errlen)
{
    size_t column;
    size_t missing_sum = 0;

    if (!dataset || !schema || !result) {
        ds_set_error(err, errlen, "missing analysis");
        return DS_ERR_ARG;
    }
    if (result->records != dataset->row_count) {
        snprintf(err, errlen, "analyzed %zu records but the dataset has %zu valid records",
                 result->records, dataset->row_count);
        return DS_ERR_FORMAT;
    }
    if (result->incomplete_records > result->records) {
        ds_set_error(err, errlen, "incomplete record count exceeds valid records");
        return DS_ERR_FORMAT;
    }
    if (result->column_count != dataset->column_count) {
        ds_set_error(err, errlen, "analysis column count does not match the dataset");
        return DS_ERR_FORMAT;
    }

    for (column = 0; column < result->column_count; column++) {
        const ColumnStats *stats = &result->columns[column];
        missing_sum += stats->missing;
        if (stats->missing > result->records) {
            snprintf(err, errlen, "column %zu missing count exceeds valid records", column);
            return DS_ERR_FORMAT;
        }
        if (schema->columns[column].kind == COL_NUMERIC) {
            if (stats->valid_numeric + stats->invalid + stats->missing != result->records) {
                snprintf(err, errlen,
                         "column '%s' counts are not a partition of the valid records",
                         dataset->headers[column]);
                return DS_ERR_FORMAT;
            }
            if (stats->out_of_range > stats->valid_numeric) {
                snprintf(err, errlen, "column '%s' has more out-of-range values than valid numbers",
                         dataset->headers[column]);
                return DS_ERR_FORMAT;
            }
        } else if (stats->valid_numeric != 0 || stats->invalid != 0 || stats->out_of_range != 0) {
            snprintf(err, errlen, "text column '%s' accumulated numeric statistics",
                     dataset->headers[column]);
            return DS_ERR_FORMAT;
        }
    }

    if (missing_sum != result->missing_cells) {
        snprintf(err, errlen, "missing cells %zu do not match the per-column total %zu",
                 result->missing_cells, missing_sum);
        return DS_ERR_FORMAT;
    }
    if ((result->incomplete_records == 0 && result->missing_cells != 0) ||
        (result->missing_cells == 0 && result->incomplete_records != 0)) {
        ds_set_error(err, errlen, "incomplete records and missing cells disagree");
        return DS_ERR_FORMAT;
    }
    if (result->missing_cells > 0 && result->incomplete_records > result->missing_cells) {
        ds_set_error(err, errlen, "incomplete records exceed missing cells");
        return DS_ERR_FORMAT;
    }
    return DS_OK;
}
