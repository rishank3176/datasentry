#include "benchmark.h"
#include "cli.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (condition) {                                                       \
            g_passed++;                                                        \
        } else {                                                               \
            g_failed++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                      \
    } while (0)

static int near(double left, double right)
{
    double difference = left - right;
    if (difference < 0.0) {
        difference = -difference;
    }
    return difference <= 1e-9;
}

static int load_text(const char *text, Dataset **out, char *err, size_t errlen)
{
    char path[] = "tests/dsXXXXXX";
    int fd;
    size_t length;
    size_t written = 0;
    int status;

    *out = NULL;
    fd = mkstemp(path);
    if (fd < 0) {
        snprintf(err, errlen, "could not create a temporary file");
        return DS_ERR_IO;
    }
    length = strlen(text);
    while (written < length) {
        ssize_t chunk = write(fd, text + written, length - written);
        if (chunk < 0) {
            close(fd);
            unlink(path);
            snprintf(err, errlen, "could not write a temporary file");
            return DS_ERR_IO;
        }
        written += (size_t)chunk;
    }
    if (close(fd) != 0) {
        unlink(path);
        snprintf(err, errlen, "could not close a temporary file");
        return DS_ERR_IO;
    }
    status = csv_load(path, out, err, errlen);
    unlink(path);
    return status;
}

static int column_of(const Dataset *dataset, const char *name)
{
    size_t index;
    for (index = 0; index < dataset->column_count; index++) {
        if (strcmp(dataset->headers[index], name) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static const char *cell_at(const Dataset *dataset, size_t row, const char *name)
{
    int column = column_of(dataset, name);
    if (column < 0 || row >= dataset->row_count) {
        return NULL;
    }
    return dataset->rows[row][column];
}

static int run_app(const char *command)
{
    int status = system(command);
    if (status < 0 || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

static void test_basic_and_quotes(void)
{
    Dataset *dataset = NULL;
    char err[256];
    int status = load_text(
        "StudentID,Name,Age\r\n"
        "S1,\"Rao, Priya\",21\r\n"
        "S2,\"She said \"\"hi\"\"\",22\r\n",
        &dataset, err, sizeof err);

    EXPECT(status == DS_OK);
    if (status != DS_OK) {
        fprintf(stderr, "  parse error: %s\n", err);
        return;
    }
    EXPECT(dataset->column_count == 3);
    EXPECT(strcmp(dataset->headers[0], "StudentID") == 0);
    EXPECT(strcmp(dataset->headers[1], "Name") == 0);
    EXPECT(strcmp(dataset->headers[2], "Age") == 0);
    EXPECT(dataset->row_count == 2);
    EXPECT(dataset->total_records == 2);
    EXPECT(dataset->malformed_count == 0);
    EXPECT(strcmp(cell_at(dataset, 0, "Name"), "Rao, Priya") == 0);
    EXPECT(strcmp(cell_at(dataset, 0, "Age"), "21") == 0);
    EXPECT(strcmp(cell_at(dataset, 1, "Name"), "She said \"hi\"") == 0);
    EXPECT(strcmp(cell_at(dataset, 1, "StudentID"), "S2") == 0);
    dataset_free(dataset);
}

static void test_missing_incomplete_and_malformed(void)
{
    Dataset *dataset = NULL;
    Schema schema;
    AnalysisResult analysis;
    char err[256];
    int status = load_text(
        "a,b\n"
        "onlyone\n"
        "x,\n"
        ",y\n"
        ",\n"
        "1,2\n"
        "1,2,3\n"
        "\n"
        "3,4\n",
        &dataset, err, sizeof err);
    int column_a;
    int column_b;

    memset(&schema, 0, sizeof schema);
    memset(&analysis, 0, sizeof analysis);
    EXPECT(status == DS_OK);
    if (status != DS_OK) {
        fprintf(stderr, "  parse error: %s\n", err);
        return;
    }

    /* onlyone, 1,2,3, and the blank line are malformed.
       A single comma is one valid record with two empty fields. */
    EXPECT(dataset->total_records == 8);
    EXPECT(dataset->malformed_count == 3);
    EXPECT(dataset->row_count == 5);

    EXPECT(schema_build(dataset, &schema, err, sizeof err) == DS_OK);
    EXPECT(analyze_serial(dataset, &schema, &analysis) == DS_OK);
    EXPECT(analysis.records == 5);
    EXPECT(analysis.incomplete_records == 3);
    EXPECT(analysis.missing_cells == 4);
    column_a = column_of(dataset, "a");
    column_b = column_of(dataset, "b");
    EXPECT(column_a >= 0 && column_b >= 0);
    EXPECT(analysis.columns[column_a].missing + analysis.columns[column_b].missing == 4);
    EXPECT(analysis_invariants(dataset, &schema, &analysis, err, sizeof err) == DS_OK);

    analysis_free(&analysis);
    schema_free(&schema);
    dataset_free(dataset);
}

static void test_detection_invalid_and_stats(void)
{
    Dataset *dataset = NULL;
    Schema schema;
    AnalysisResult analysis;
    char err[256];
    int status = load_text(
        "score,note,empty_only\n"
        "10,hello,\n"
        "20,world, \n"
        "30,7,\n"
        "40,8,\n"
        "  50  ,9,\n"
        "60,10,\n"
        "70,nope,\n"
        "nan,11,\n"
        "inf,12,\n"
        "1e2,later,\n",
        &dataset, err, sizeof err);
    int score;
    int note;
    int empty_only;

    memset(&schema, 0, sizeof schema);
    memset(&analysis, 0, sizeof analysis);
    EXPECT(status == DS_OK);
    if (status != DS_OK) {
        return;
    }
    EXPECT(schema_build(dataset, &schema, err, sizeof err) == DS_OK);
    score = column_of(dataset, "score");
    note = column_of(dataset, "note");
    empty_only = column_of(dataset, "empty_only");
    EXPECT(score >= 0 && note >= 0 && empty_only >= 0);

    /* 8 finite values of 10 non-missing score cells: 10,20,30,40,50,60,70,100. */
    EXPECT(schema.columns[score].kind == COL_NUMERIC);
    EXPECT(schema.columns[score].numeric_parsed == 8);
    /* 6 of 10 note values are numeric, which is below 70%. */
    EXPECT(schema.columns[note].kind == COL_TEXT);
    EXPECT(schema.columns[note].numeric_parsed == 6);
    EXPECT(schema.columns[empty_only].kind == COL_TEXT);
    EXPECT(schema.columns[empty_only].non_missing == 0);

    EXPECT(analyze_serial(dataset, &schema, &analysis) == DS_OK);
    EXPECT(analysis.columns[score].valid_numeric == 8);
    EXPECT(analysis.columns[score].invalid == 2);
    EXPECT(analysis.columns[score].missing == 0);
    EXPECT(near(analysis.columns[score].min_value, 10.0));
    EXPECT(near(analysis.columns[score].max_value, 100.0));
    EXPECT(near(column_mean(&analysis.columns[score]), 47.5));
    EXPECT(analysis.columns[note].valid_numeric == 0);
    EXPECT(analysis.columns[note].invalid == 0);
    EXPECT(analysis.columns[empty_only].missing == 10);
    EXPECT(analysis.incomplete_records == 10);
    EXPECT(analysis_invariants(dataset, &schema, &analysis, err, sizeof err) == DS_OK);

    analysis_free(&analysis);
    schema_free(&schema);
    dataset_free(dataset);
}

static void test_threshold_boundaries(void)
{
    Dataset *numeric = NULL;
    Dataset *text = NULL;
    Schema numeric_schema;
    Schema text_schema;
    char err[256];

    memset(&numeric_schema, 0, sizeof numeric_schema);
    memset(&text_schema, 0, sizeof text_schema);

    EXPECT(load_text("n\n1\n2\n3\n4\n5\n6\n7\na\nb\nc\n",
                     &numeric, err, sizeof err) == DS_OK);
    EXPECT(load_text("n\n1\n2\n3\n4\n5\n6\na\nb\nc\nd\n",
                     &text, err, sizeof err) == DS_OK);
    if (!numeric || !text) {
        dataset_free(numeric);
        dataset_free(text);
        return;
    }
    EXPECT(schema_build(numeric, &numeric_schema, err, sizeof err) == DS_OK);
    EXPECT(schema_build(text, &text_schema, err, sizeof err) == DS_OK);
    EXPECT(numeric_schema.columns[0].kind == COL_NUMERIC);
    EXPECT(text_schema.columns[0].kind == COL_TEXT);

    schema_free(&numeric_schema);
    schema_free(&text_schema);
    dataset_free(numeric);
    dataset_free(text);
}

static void test_ranges(void)
{
    Dataset *dataset = NULL;
    Schema schema;
    AnalysisResult serial_result;
    AnalysisResult parallel_result;
    ParallelInfo info;
    char err[256];
    int value_col;

    memset(&schema, 0, sizeof schema);
    memset(&serial_result, 0, sizeof serial_result);
    memset(&parallel_result, 0, sizeof parallel_result);

    EXPECT(load_text("value\n-1\n0\n50\n100\n101\nabc\n\n",
                     &dataset, err, sizeof err) == DS_OK);
    if (!dataset) {
        return;
    }
    EXPECT(schema_build(dataset, &schema, err, sizeof err) == DS_OK);
    EXPECT(schema_apply_range(&schema, dataset, "value", 0.0, 100.0, err, sizeof err) == DS_OK);
    EXPECT(schema_apply_range(&schema, dataset, "value", 0.0, 10.0, err, sizeof err) == DS_ERR_ARG);
    EXPECT(schema_apply_range(&schema, dataset, "missing", 0.0, 1.0, err, sizeof err) == DS_ERR_ARG);

    EXPECT(analyze_serial(dataset, &schema, &serial_result) == DS_OK);
    value_col = column_of(dataset, "value");
    EXPECT(value_col == 0);
    EXPECT(serial_result.columns[0].valid_numeric == 5);
    EXPECT(serial_result.columns[0].invalid == 1);
    EXPECT(serial_result.columns[0].missing == 1);
    EXPECT(serial_result.columns[0].out_of_range == 2);
    EXPECT(near(serial_result.columns[0].min_value, -1.0));
    EXPECT(near(serial_result.columns[0].max_value, 101.0));
    EXPECT(near(column_mean(&serial_result.columns[0]), 50.0));
    EXPECT(serial_result.incomplete_records == 1);
    EXPECT(analysis_invariants(dataset, &schema, &serial_result, err, sizeof err) == DS_OK);

    EXPECT(analyze_parallel(dataset, &schema, 4, &info, &parallel_result) == DS_OK);
    EXPECT(analysis_equivalent(&serial_result, &parallel_result, DS_COMPARE_TOLERANCE));
    EXPECT(info.effective_workers == 4);
    EXPECT(info.fallback == 0);

    analysis_free(&serial_result);
    analysis_free(&parallel_result);
    schema_free(&schema);
    dataset_free(dataset);
}

static void test_parallel_partitions(void)
{
    char text[2048];
    size_t used = 0;
    int row;
    Dataset *dataset = NULL;
    Schema schema;
    AnalysisResult serial_result;
    AnalysisResult one_worker;
    AnalysisResult many_workers;
    AnalysisResult capped;
    ParallelInfo one_info;
    ParallelInfo many_info;
    ParallelInfo capped_info;
    BenchmarkResult bench;
    AnalysisResult bench_result;
    char err[256];
    int written;

    memset(&schema, 0, sizeof schema);
    memset(&serial_result, 0, sizeof serial_result);
    memset(&one_worker, 0, sizeof one_worker);
    memset(&many_workers, 0, sizeof many_workers);
    memset(&capped, 0, sizeof capped);
    memset(&bench_result, 0, sizeof bench_result);

    written = snprintf(text, sizeof text, "id,measurement\n");
    EXPECT(written > 0);
    used = (size_t)written;
    for (row = 0; row < 17; row++) {
        int value = row == 0 ? 1000 : (row == 16 ? -5 : row);
        written = snprintf(text + used, sizeof text - used, "r%d,%d\n", row, value);
        EXPECT(written > 0);
        used += (size_t)written;
    }

    EXPECT(load_text(text, &dataset, err, sizeof err) == DS_OK);
    if (!dataset) {
        return;
    }
    EXPECT(dataset->row_count == 17);
    EXPECT(schema_build(dataset, &schema, err, sizeof err) == DS_OK);
    EXPECT(schema.columns[column_of(dataset, "measurement")].kind == COL_NUMERIC);
    EXPECT(schema.columns[column_of(dataset, "id")].kind == COL_TEXT);
    EXPECT(schema_apply_range(&schema, dataset, "id", 0.0, 10.0, err, sizeof err) == DS_ERR_ARG);
    EXPECT(schema_apply_range(&schema, dataset, "measurement", -5.0, 1000.0, err, sizeof err) == DS_OK);

    EXPECT(analyze_serial(dataset, &schema, &serial_result) == DS_OK);
    EXPECT(serial_result.columns[1].valid_numeric == 17);
    EXPECT(near(serial_result.columns[1].min_value, -5.0));
    EXPECT(near(serial_result.columns[1].max_value, 1000.0));
    EXPECT(serial_result.columns[1].out_of_range == 0);
    EXPECT(analysis_invariants(dataset, &schema, &serial_result, err, sizeof err) == DS_OK);

    EXPECT(analyze_parallel(dataset, &schema, 1, &one_info, &one_worker) == DS_OK);
    EXPECT(one_info.requested_threads == 1);
    EXPECT(one_info.effective_workers == 1);
    EXPECT(one_info.fallback == 0);
    EXPECT(analysis_equivalent(&serial_result, &one_worker, DS_COMPARE_TOLERANCE));

    EXPECT(analyze_parallel(dataset, &schema, 4, &many_info, &many_workers) == DS_OK);
    EXPECT(many_info.effective_workers == 4);
    EXPECT(analysis_equivalent(&serial_result, &many_workers, DS_COMPARE_TOLERANCE));

    EXPECT(analyze_parallel(dataset, &schema, 64, &capped_info, &capped) == DS_OK);
    EXPECT(capped_info.requested_threads == 64);
    EXPECT(capped_info.effective_workers == 17);
    EXPECT(capped_info.planned_workers == 17);
    EXPECT(strstr(capped_info.mode, "limited by the number of valid records") != NULL);
    EXPECT(analysis_equivalent(&serial_result, &capped, DS_COMPARE_TOLERANCE));

    EXPECT(benchmark_run(dataset, &schema, 4, &bench, &bench_result, &many_info) == DS_OK);
    EXPECT(bench.equivalent == 1);
    EXPECT(bench.serial_ms >= 0.0);
    EXPECT(bench.parallel_ms >= 0.0);
    EXPECT(analysis_equivalent(&serial_result, &bench_result, DS_COMPARE_TOLERANCE));

    analysis_free(&serial_result);
    analysis_free(&one_worker);
    analysis_free(&many_workers);
    analysis_free(&capped);
    analysis_free(&bench_result);
    schema_free(&schema);
    dataset_free(dataset);
}

static void test_empty_header_only_and_io(void)
{
    Dataset *dataset = NULL;
    Schema schema;
    AnalysisResult serial_result;
    AnalysisResult parallel_result;
    ParallelInfo info;
    char err[256];
    char path[] = "tests/dsemptyXXXXXX";
    int fd;

    memset(&schema, 0, sizeof schema);
    memset(&serial_result, 0, sizeof serial_result);
    memset(&parallel_result, 0, sizeof parallel_result);

    EXPECT(csv_load("tests/does-not-exist.csv", &dataset, err, sizeof err) == DS_ERR_IO);
    EXPECT(dataset == NULL);
    EXPECT(strstr(err, "cannot open") != NULL);

    fd = mkstemp(path);
    EXPECT(fd >= 0);
    if (fd >= 0) {
        close(fd);
        EXPECT(csv_load(path, &dataset, err, sizeof err) == DS_ERR_FORMAT);
        EXPECT(dataset == NULL);
        EXPECT(strstr(err, "empty") != NULL);
        unlink(path);
    }

    EXPECT(load_text("only,header\n", &dataset, err, sizeof err) == DS_OK);
    if (dataset) {
        EXPECT(dataset->row_count == 0);
        EXPECT(dataset->total_records == 0);
        EXPECT(schema_build(dataset, &schema, err, sizeof err) == DS_OK);
        EXPECT(schema.columns[0].kind == COL_TEXT);
        EXPECT(analyze_serial(dataset, &schema, &serial_result) == DS_OK);
        EXPECT(analyze_parallel(dataset, &schema, 4, &info, &parallel_result) == DS_OK);
        EXPECT(info.effective_workers == 0);
        EXPECT(analysis_equivalent(&serial_result, &parallel_result, DS_COMPARE_TOLERANCE));
        EXPECT(serial_result.records == 0);
        EXPECT(serial_result.missing_cells == 0);
        analysis_free(&serial_result);
        analysis_free(&parallel_result);
        schema_free(&schema);
        dataset_free(dataset);
    }

    EXPECT(load_text("\n", &dataset, err, sizeof err) == DS_ERR_FORMAT);
    EXPECT(dataset == NULL);
    EXPECT(load_text("Name,Name\nA,B\n", &dataset, err, sizeof err) == DS_ERR_FORMAT);
    EXPECT(dataset == NULL);
    EXPECT(load_text("\"unterminated\n", &dataset, err, sizeof err) == DS_ERR_FORMAT);
    EXPECT(dataset == NULL);
}

static void test_unclosed_quote_is_skipped(void)
{
    Dataset *dataset = NULL;
    char err[256];
    int status = load_text("a,b\n\"oops,1\n2,3\n", &dataset, err, sizeof err);

    EXPECT(status == DS_OK);
    if (status != DS_OK) {
        return;
    }
    EXPECT(dataset->total_records == 2);
    EXPECT(dataset->malformed_count == 1);
    EXPECT(dataset->row_count == 1);
    EXPECT(strcmp(dataset->rows[0][0], "2") == 0);
    EXPECT(strcmp(dataset->rows[0][1], "3") == 0);
    dataset_free(dataset);
}

static void test_cli_and_process(void)
{
    Options options;
    char err[256];
    char *bad_unknown[] = {"datasentry", "--turbo", NULL};
    char *bad_threads[] = {"datasentry", "--threads", "0", "data.csv", NULL};
    char *bad_high[] = {"datasentry", "--threads", "65", NULL};
    char *bad_text[] = {"datasentry", "--threads", "4abc", NULL};
    char *bad_missing[] = {"datasentry", "--threads", NULL};
    char *bad_range[] = {"datasentry", "--range", "Age:10:10", NULL};
    char *bad_nan[] = {"datasentry", "--range", "Age:nan:1", NULL};
    char *bad_inf[] = {"datasentry", "--range", "Age:0:inf", NULL};
    char *bad_shape[] = {"datasentry", "--range", "Age", NULL};
    char *extra[] = {"datasentry", "one.csv", "two.csv", NULL};
    char *good[] = {
        "datasentry", "--benchmark", "--threads", "8",
        "--range", "Full Name:0:10", "--range", "Temp:-5.5:40",
        "data/sample.csv", NULL
    };

    options_init(&options);
    EXPECT(options_parse(2, bad_unknown, &options, err, sizeof err) == DS_ERR_ARG);
    EXPECT(strstr(err, "unknown option") != NULL);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(4, bad_threads, &options, err, sizeof err) == DS_ERR_ARG);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(3, bad_high, &options, err, sizeof err) == DS_ERR_ARG);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(3, bad_text, &options, err, sizeof err) == DS_ERR_ARG);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(2, bad_missing, &options, err, sizeof err) == DS_ERR_ARG);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(3, bad_range, &options, err, sizeof err) == DS_ERR_ARG);
    EXPECT(strstr(err, "minimum must be less than maximum") != NULL);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(3, bad_nan, &options, err, sizeof err) == DS_ERR_ARG);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(3, bad_inf, &options, err, sizeof err) == DS_ERR_ARG);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(3, bad_shape, &options, err, sizeof err) == DS_ERR_ARG);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(3, extra, &options, err, sizeof err) == DS_ERR_ARG);
    options_free(&options);

    options_init(&options);
    EXPECT(options_parse(9, good, &options, err, sizeof err) == DS_OK);
    EXPECT(options.benchmark == 1);
    EXPECT(options.threads == 8);
    EXPECT(options.range_count == 2);
    EXPECT(strcmp(options.ranges[0].column, "Full Name") == 0);
    EXPECT(near(options.ranges[0].min_value, 0.0));
    EXPECT(near(options.ranges[0].max_value, 10.0));
    EXPECT(strcmp(options.ranges[1].column, "Temp") == 0);
    EXPECT(near(options.ranges[1].min_value, -5.5));
    EXPECT(near(options.ranges[1].max_value, 40.0));
    EXPECT(strcmp(options.dataset_path, "data/sample.csv") == 0);
    options_free(&options);

    options_init(&options);
    {
        char *help_argv[] = {"datasentry", "data.csv", "--help", NULL};
        EXPECT(options_parse(3, help_argv, &options, err, sizeof err) == DS_OK);
        EXPECT(options.help == 1);
    }
    options_free(&options);

    EXPECT(run_app("./datasentry --help >/dev/null") == 0);
    EXPECT(run_app("./datasentry --bogus >/dev/null 2>&1") == 1);
    EXPECT(run_app("./datasentry >/dev/null 2>&1") == 1);
    EXPECT(run_app("./datasentry tests/does-not-exist.csv >/dev/null 2>&1") == 1);
    EXPECT(run_app("./datasentry --threads 0 data/sample.csv >/dev/null 2>&1") == 1);
    EXPECT(run_app("./datasentry data/sample.csv --range 'Name:0:1' >/dev/null 2>&1") == 1);
    EXPECT(run_app("./datasentry data/sample.csv --range 'NoSuch:0:1' >/dev/null 2>&1") == 1);
    EXPECT(run_app("./datasentry data/sample.csv --threads 4 --benchmark "
                   "--range 'Age:0:120' --range 'Attendance:0:100' "
                   "--range 'Score:0:100' >/dev/null") == 0);
}

static void test_sample_dataset(void)
{
    Dataset *dataset = NULL;
    Schema schema;
    AnalysisResult analysis;
    ParallelInfo info;
    char err[256];
    int age;
    int attendance;
    int score;
    int name;
    int email;
    int id_col;
    size_t row;
    int found_comma_name = 0;
    int found_quote = 0;

    memset(&schema, 0, sizeof schema);
    memset(&analysis, 0, sizeof analysis);

    EXPECT(csv_load("data/sample.csv", &dataset, err, sizeof err) == DS_OK);
    if (!dataset) {
        fprintf(stderr, "  sample load: %s\n", err);
        return;
    }
    EXPECT(dataset->column_count == 6);
    EXPECT(dataset->total_records == 42);
    EXPECT(dataset->row_count == 40);
    EXPECT(dataset->malformed_count == 2);

    for (row = 0; row < dataset->row_count; row++) {
        if (strcmp(dataset->rows[row][0], "S002") == 0 &&
            strcmp(dataset->rows[row][1], "Rao, Priya") == 0) {
            found_comma_name = 1;
        }
        if (strcmp(dataset->rows[row][0], "S023") == 0 &&
            strcmp(dataset->rows[row][1], "He said \"hello\"") == 0) {
            found_quote = 1;
        }
    }
    EXPECT(found_comma_name);
    EXPECT(found_quote);

    EXPECT(schema_build(dataset, &schema, err, sizeof err) == DS_OK);
    age = column_of(dataset, "Age");
    attendance = column_of(dataset, "Attendance");
    score = column_of(dataset, "Score");
    name = column_of(dataset, "Name");
    email = column_of(dataset, "Email");
    id_col = column_of(dataset, "StudentID");
    EXPECT(schema.columns[age].kind == COL_NUMERIC);
    EXPECT(schema.columns[attendance].kind == COL_NUMERIC);
    EXPECT(schema.columns[score].kind == COL_NUMERIC);
    EXPECT(schema.columns[name].kind == COL_TEXT);
    EXPECT(schema.columns[email].kind == COL_TEXT);
    EXPECT(schema.columns[id_col].kind == COL_TEXT);

    EXPECT(schema_apply_range(&schema, dataset, "Age", 0.0, 120.0, err, sizeof err) == DS_OK);
    EXPECT(schema_apply_range(&schema, dataset, "Attendance", 0.0, 100.0, err, sizeof err) == DS_OK);
    EXPECT(schema_apply_range(&schema, dataset, "Score", 0.0, 100.0, err, sizeof err) == DS_OK);
    EXPECT(schema_apply_range(&schema, dataset, "Name", 0.0, 1.0, err, sizeof err) == DS_ERR_ARG);

    EXPECT(analyze_parallel(dataset, &schema, 4, &info, &analysis) == DS_OK);
    EXPECT(info.effective_workers == 4);
    EXPECT(analysis.records == 40);
    EXPECT(analysis.incomplete_records == 4);
    EXPECT(analysis.missing_cells == 5);
    EXPECT(analysis.columns[age].missing == 1);
    EXPECT(analysis.columns[age].invalid == 1);
    EXPECT(analysis.columns[age].valid_numeric == 38);
    EXPECT(analysis.columns[age].out_of_range == 2);
    EXPECT(near(analysis.columns[age].min_value, -2.0));
    EXPECT(near(analysis.columns[age].max_value, 150.0));
    EXPECT(analysis.columns[attendance].missing == 1);
    EXPECT(analysis.columns[attendance].invalid == 1);
    EXPECT(analysis.columns[attendance].valid_numeric == 38);
    EXPECT(analysis.columns[attendance].out_of_range == 1);
    EXPECT(near(analysis.columns[attendance].min_value, 0.0));
    EXPECT(near(analysis.columns[attendance].max_value, 142.0));
    EXPECT(analysis.columns[score].missing == 2);
    EXPECT(analysis.columns[score].invalid == 1);
    EXPECT(analysis.columns[score].valid_numeric == 37);
    EXPECT(analysis.columns[score].out_of_range == 1);
    EXPECT(near(analysis.columns[score].min_value, 60.0));
    EXPECT(near(analysis.columns[score].max_value, 105.0));
    EXPECT(analysis.columns[email].missing == 1);
    EXPECT(analysis_invariants(dataset, &schema, &analysis, err, sizeof err) == DS_OK);

    analysis_free(&analysis);
    schema_free(&schema);
    dataset_free(dataset);
}

static void test_long_column_name(void)
{
    Dataset *dataset = NULL;
    Schema schema;
    AnalysisResult analysis;
    char err[256];
    const char *header = "VeryLongColumnNameIndeed";

    memset(&schema, 0, sizeof schema);
    memset(&analysis, 0, sizeof analysis);
    EXPECT(load_text("VeryLongColumnNameIndeed\n50\n", &dataset, err, sizeof err) == DS_OK);
    if (!dataset) {
        return;
    }
    EXPECT(strcmp(dataset->headers[0], header) == 0);
    EXPECT(schema_build(dataset, &schema, err, sizeof err) == DS_OK);
    EXPECT(schema_apply_range(&schema, dataset, header, 0.0, 10.0, err, sizeof err) == DS_OK);
    EXPECT(analyze_serial(dataset, &schema, &analysis) == DS_OK);
    EXPECT(analysis.columns[0].out_of_range == 1);
    EXPECT(analysis.columns[0].valid_numeric == 1);

    analysis_free(&analysis);
    schema_free(&schema);
    dataset_free(dataset);
}

int main(void)
{
    setlocale(LC_ALL, "C");

    test_basic_and_quotes();
    test_missing_incomplete_and_malformed();
    test_detection_invalid_and_stats();
    test_threshold_boundaries();
    test_ranges();
    test_parallel_partitions();
    test_empty_header_only_and_io();
    test_unclosed_quote_is_skipped();
    test_cli_and_process();
    test_sample_dataset();
    test_long_column_name();

    printf("%d checks passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
