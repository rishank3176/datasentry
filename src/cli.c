#include "cli.h"

#include "analyzer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void options_init(Options *options)
{
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->threads = DS_DEFAULT_THREADS;
}

void options_free(Options *options)
{
    size_t index;

    if (!options) {
        return;
    }
    free(options->dataset_path);
    options->dataset_path = NULL;
    if (options->ranges) {
        for (index = 0; index < options->range_count; index++) {
            free(options->ranges[index].column);
        }
    }
    free(options->ranges);
    options->ranges = NULL;
    options->range_count = 0;
}

static int parse_thread_count(const char *text, int *out, char *err, size_t errlen)
{
    char *end;
    long value;
    const char *cursor;

    if (!text || text[0] == '\0') {
        ds_set_error(err, errlen, "missing thread count");
        return DS_ERR_ARG;
    }
    for (cursor = text; *cursor; cursor++) {
        if (!isdigit((unsigned char)*cursor)) {
            snprintf(err, errlen,
                     "invalid thread count '%s' (expected an integer from 1 to %d)",
                     text, DS_MAX_THREADS);
            return DS_ERR_ARG;
        }
    }
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 1 || value > DS_MAX_THREADS) {
        snprintf(err, errlen,
                 "invalid thread count '%s' (expected an integer from 1 to %d)",
                 text, DS_MAX_THREADS);
        return DS_ERR_ARG;
    }
    *out = (int)value;
    return DS_OK;
}

/*
 * Column:min:max, split on the last two colons so a column name may contain
 * colons or spaces. Bounds are parsed with the same finite-number rules as cells.
 */
static int parse_range_spec(const char *spec, RangeSpec *range, char *err, size_t errlen)
{
    const char *max_colon;
    const char *min_colon;
    const char *cursor;
    char *name;
    char *min_text;
    char *max_text;
    size_t name_len;
    size_t min_len;

    if (!spec || spec[0] == '\0') {
        ds_set_error(err, errlen, "missing range specification");
        return DS_ERR_ARG;
    }
    max_colon = strrchr(spec, ':');
    min_colon = NULL;
    if (max_colon) {
        for (cursor = spec; cursor < max_colon; cursor++) {
            if (*cursor == ':') {
                min_colon = cursor;
            }
        }
    }
    if (!min_colon || min_colon == spec || max_colon == min_colon + 1) {
        snprintf(err, errlen, "invalid range '%s' (expected Column:min:max)", spec);
        return DS_ERR_ARG;
    }

    name_len = (size_t)(min_colon - spec);
    min_len = (size_t)(max_colon - (min_colon + 1));
    name = malloc(name_len + 1);
    min_text = malloc(min_len + 1);
    max_text = ds_strdup(max_colon + 1);
    if (!name || !min_text || !max_text) {
        free(name);
        free(min_text);
        free(max_text);
        ds_set_error(err, errlen, "out of memory while parsing a range");
        return DS_ERR_NOMEM;
    }
    memcpy(name, spec, name_len);
    name[name_len] = '\0';
    memcpy(min_text, min_colon + 1, min_len);
    min_text[min_len] = '\0';

    range->column = ds_trim_dup(name);
    free(name);
    if (!range->column) {
        free(min_text);
        free(max_text);
        ds_set_error(err, errlen, "out of memory while parsing a range");
        return DS_ERR_NOMEM;
    }
    if (range->column[0] == '\0') {
        free(range->column);
        range->column = NULL;
        free(min_text);
        free(max_text);
        snprintf(err, errlen, "invalid range '%s' (column name is empty)", spec);
        return DS_ERR_ARG;
    }
    if (!ds_parse_finite(min_text, &range->min_value) ||
        !ds_parse_finite(max_text, &range->max_value)) {
        snprintf(err, errlen, "invalid range '%s' (bounds must be finite numbers)", spec);
        free(range->column);
        range->column = NULL;
        free(min_text);
        free(max_text);
        return DS_ERR_ARG;
    }
    free(min_text);
    free(max_text);
    if (!(range->min_value < range->max_value)) {
        snprintf(err, errlen,
                 "invalid range '%s' (minimum must be less than maximum)", spec);
        free(range->column);
        range->column = NULL;
        return DS_ERR_ARG;
    }
    return DS_OK;
}

static int add_range(Options *options, RangeSpec *range)
{
    RangeSpec *grown;
    size_t next = options->range_count + 1;

    grown = realloc(options->ranges, next * sizeof(*grown));
    if (!grown) {
        return DS_ERR_NOMEM;
    }
    options->ranges = grown;
    options->ranges[options->range_count] = *range;
    options->range_count = next;
    return DS_OK;
}

int options_parse(int argc, char **argv, Options *options, char *err, size_t errlen)
{
    int index;
    int threads_set = 0;

    if (!options) {
        ds_set_error(err, errlen, "missing options");
        return DS_ERR_ARG;
    }

    for (index = 1; index < argc; index++) {
        const char *argument = argv[index];
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            options->help = 1;
            return DS_OK;
        }
        if (strcmp(argument, "--benchmark") == 0) {
            options->benchmark = 1;
            continue;
        }
        if (strcmp(argument, "--threads") == 0) {
            int threads;
            int status;
            if (index + 1 >= argc) {
                ds_set_error(err, errlen, "missing value for --threads");
                return DS_ERR_ARG;
            }
            if (threads_set) {
                ds_set_error(err, errlen, "--threads was specified more than once");
                return DS_ERR_ARG;
            }
            index++;
            status = parse_thread_count(argv[index], &threads, err, errlen);
            if (status != DS_OK) {
                return status;
            }
            options->threads = threads;
            threads_set = 1;
            continue;
        }
        if (strcmp(argument, "--range") == 0) {
            RangeSpec range;
            int status;
            if (index + 1 >= argc) {
                ds_set_error(err, errlen, "missing value for --range");
                return DS_ERR_ARG;
            }
            index++;
            memset(&range, 0, sizeof(range));
            status = parse_range_spec(argv[index], &range, err, errlen);
            if (status != DS_OK) {
                return status;
            }
            status = add_range(options, &range);
            if (status != DS_OK) {
                free(range.column);
                ds_set_error(err, errlen, "out of memory while storing a range");
                return status;
            }
            continue;
        }
        if (argument[0] == '-') {
            snprintf(err, errlen, "unknown option '%s'", argument);
            return DS_ERR_ARG;
        }
        if (options->dataset_path) {
            snprintf(err, errlen, "unexpected extra argument '%s'", argument);
            return DS_ERR_ARG;
        }
        options->dataset_path = ds_strdup(argument);
        if (!options->dataset_path) {
            ds_set_error(err, errlen, "out of memory while storing the dataset path");
            return DS_ERR_NOMEM;
        }
    }
    return DS_OK;
}

void options_print_help(FILE *out)
{
    fprintf(out,
            "DataSentry — Parallel Dataset Quality Analyzer\n"
            "\n"
            "Usage:\n"
            "  datasentry <file.csv> [options]\n"
            "  datasentry --help\n"
            "\n"
            "Options:\n"
            "  --threads N     Worker threads for parallel analysis.\n"
            "                  Default: %d. Maximum: %d. Values outside 1..%d are rejected.\n"
            "  --benchmark     Run the same analysis serially and with worker threads,\n"
            "                  then report both times and whether the results match.\n"
            "  --range SPEC    Inclusive numeric limits. SPEC is Column:min:max.\n"
            "                  Repeat the option for more than one column. Quote SPEC\n"
            "                  when the column name contains spaces, for example\n"
            "                  --range \"Full Name:0:10\". Column names are case-sensitive.\n"
            "                  The minimum must be finite and strictly less than the maximum.\n"
            "  -h, --help      Show this help and exit successfully.\n"
            "\n"
            "Examples:\n"
            "  datasentry data/sample.csv\n"
            "  datasentry data/sample.csv --threads 4\n"
            "  datasentry data/sample.csv --threads 4 --benchmark\n"
            "  datasentry data/sample.csv --threads 4 --benchmark \\\n"
            "      --range \"Age:0:120\" --range \"Attendance:0:100\" --range \"Score:0:100\"\n"
            "\n"
            "Exit status:\n"
            "  0  The dataset was analyzed. With --benchmark, serial and parallel\n"
            "     results also matched.\n"
            "  1  Invalid arguments, an unreadable or malformed file, a range rule\n"
            "     that does not apply, or benchmark results that disagree.\n"
            "\n"
            "Notes:\n"
            "  Parsing the CSV and detecting column types happens before the timed\n"
            "  section. Benchmark timing includes thread creation, joining, and merging.\n"
            "  A column is treated as numeric when at least 70%% of its non-missing\n"
            "  values are finite numbers and at least one such value exists.\n"
            "  Quoted fields cannot span multiple lines. Numbers use a '.' decimal point.\n"
            "  If fewer valid records exist than requested threads, the extra threads\n"
            "  are not created. If thread creation fails, the remaining records are\n"
            "  analyzed on the main thread and the report says so.\n",
            DS_DEFAULT_THREADS, DS_MAX_THREADS, DS_MAX_THREADS);
}
