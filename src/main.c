#include "report.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    Options options;
    Dataset *dataset = NULL;
    Schema schema;
    AnalysisResult analysis;
    ParallelInfo parallel_info;
    BenchmarkResult benchmark;
    char err[512];
    char invariant_warning[576];
    const char *warnings[8];
    size_t warning_count = 0;
    size_t range_index;
    int status;
    int exit_code = 0;
    Report report;

    setlocale(LC_ALL, "C");
    memset(&schema, 0, sizeof(schema));
    memset(&analysis, 0, sizeof(analysis));
    memset(&parallel_info, 0, sizeof(parallel_info));
    memset(&benchmark, 0, sizeof(benchmark));
    options_init(&options);

    status = options_parse(argc, argv, &options, err, sizeof err);
    if (status != DS_OK) {
        fprintf(stderr, "datasentry: %s\n", err);
        fprintf(stderr, "Try 'datasentry --help' for usage.\n");
        options_free(&options);
        return 1;
    }
    if (options.help) {
        options_print_help(stdout);
        options_free(&options);
        return 0;
    }
    if (!options.dataset_path) {
        fprintf(stderr, "datasentry: missing dataset path\n");
        fprintf(stderr, "Try 'datasentry --help' for usage.\n");
        options_free(&options);
        return 1;
    }

    status = csv_load(options.dataset_path, &dataset, err, sizeof err);
    if (status != DS_OK) {
        fprintf(stderr, "datasentry: %s\n", err);
        options_free(&options);
        return 1;
    }
    status = schema_build(dataset, &schema, err, sizeof err);
    if (status != DS_OK) {
        fprintf(stderr, "datasentry: %s\n", err);
        dataset_free(dataset);
        options_free(&options);
        return 1;
    }
    for (range_index = 0; range_index < options.range_count; range_index++) {
        status = schema_apply_range(&schema, dataset,
                                    options.ranges[range_index].column,
                                    options.ranges[range_index].min_value,
                                    options.ranges[range_index].max_value,
                                    err, sizeof err);
        if (status != DS_OK) {
            fprintf(stderr, "datasentry: %s\n", err);
            schema_free(&schema);
            dataset_free(dataset);
            options_free(&options);
            return 1;
        }
    }

    if (options.benchmark) {
        status = benchmark_run(dataset, &schema, options.threads, &benchmark,
                               &analysis, &parallel_info);
    } else {
        status = analyze_parallel(dataset, &schema, options.threads, &parallel_info,
                                  &analysis);
    }
    if (status != DS_OK) {
        fprintf(stderr, "datasentry: analysis failed (%d)\n", status);
        analysis_free(&analysis);
        schema_free(&schema);
        dataset_free(dataset);
        options_free(&options);
        return 1;
    }

    status = analysis_invariants(dataset, &schema, &analysis, err, sizeof err);
    if (status != DS_OK && warning_count < 8) {
        snprintf(invariant_warning, sizeof invariant_warning, "%s", err);
        warnings[warning_count++] = invariant_warning;
        fprintf(stderr, "datasentry: %s\n", err);
        exit_code = 1;
    }
    if (parallel_info.fallback && warning_count < 8) {
        warnings[warning_count++] =
            "thread creation failed; remaining records were analyzed on the main thread";
    }
    if (options.benchmark && !benchmark.equivalent) {
        if (warning_count < 8) {
            warnings[warning_count++] =
                "serial and parallel results did not match; the summary shows the serial result";
        }
        exit_code = 1;
    }

    memset(&report, 0, sizeof(report));
    report.path = options.dataset_path;
    report.dataset = dataset;
    report.schema = &schema;
    report.analysis = &analysis;
    report.parallel = &parallel_info;
    report.benchmark = options.benchmark ? &benchmark : NULL;
    report.warnings = warnings;
    report.warning_count = warning_count;
    report_print(stdout, &report);

    analysis_free(&analysis);
    schema_free(&schema);
    dataset_free(dataset);
    options_free(&options);
    return exit_code;
}
