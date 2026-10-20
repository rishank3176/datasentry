#ifndef DATASENTRY_CSV_H
#define DATASENTRY_CSV_H

#include "util.h"

#include <stddef.h>

/*
 * In-memory CSV dataset.
 *
 * headers[c] is the trimmed column name.
 * rows[r][c] is the field text for a structurally valid record. Empty and
 * whitespace-only strings are retained; the analyzer treats them as missing.
 * Malformed records are counted and discarded, not stored.
 * Every string is owned by the dataset and released by dataset_free.
 */
typedef struct Dataset {
    char **headers;
    size_t column_count;
    char ***rows;
    size_t row_count;
    size_t total_records;
    size_t malformed_count;
} Dataset;

/*
 * Loads a CSV file into *out.
 * On success *out is a new dataset. On failure *out is NULL and nothing is leaked.
 * Multiline quoted fields are not supported: a quote must close on the same line.
 */
int csv_load(const char *path, Dataset **out, char *err, size_t errlen);

void dataset_free(Dataset *dataset);

#endif
