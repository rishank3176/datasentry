#include "report.h"

#include <stdio.h>
#include <string.h>

static void truncate_label(const char *name, char *buffer, size_t buffer_len)
{
    size_t length;
    size_t keep;

    if (!buffer || buffer_len == 0) {
        return;
    }
    if (!name) {
        buffer[0] = '\0';
        return;
    }
    length = strlen(name);
    if (length < buffer_len) {
        memcpy(buffer, name, length + 1);
        return;
    }
    if (buffer_len <= 3) {
        size_t index;
        for (index = 0; index + 1 < buffer_len; index++) {
            buffer[index] = '.';
        }
        buffer[buffer_len - 1] = '\0';
        return;
    }
    keep = buffer_len - 3;
    memcpy(buffer, name, keep);
    buffer[keep] = '.';
    buffer[keep + 1] = '.';
    buffer[keep + 2] = '\0';
}

static void format_ms(char *buffer, size_t buffer_len, double milliseconds)
{
    if (milliseconds < 0.01) {
        snprintf(buffer, buffer_len, "%.6f ms", milliseconds);
    } else {
        snprintf(buffer, buffer_len, "%.3f ms", milliseconds);
    }
}

static void print_separator(FILE *out, const char *title)
{
    fprintf(out, "\n== %s ==\n", title);
}

void report_print(FILE *out, const Report *report)
{
    size_t column;
    char serial_time[64];
    char parallel_time[64];

    if (!out || !report || !report->dataset || !report->schema || !report->analysis) {
        return;
    }

    fprintf(out, "DataSentry — Parallel Dataset Quality Analyzer\n");

    print_separator(out, "Dataset");
    fprintf(out, "File:              %s\n", report->path ? report->path : "(unknown)");
    fprintf(out, "Columns:           %zu\n", report->dataset->column_count);
    fprintf(out, "Column names:      ");
    for (column = 0; column < report->dataset->column_count; column++) {
        fprintf(out, "%s%s", column == 0 ? "" : ", ", report->dataset->headers[column]);
    }
    fprintf(out, "\n");
    fprintf(out, "Data records:      %zu\n", report->dataset->total_records);
    fprintf(out, "Valid records:     %zu\n", report->dataset->row_count);
    fprintf(out, "Malformed records: %zu\n", report->dataset->malformed_count);

    print_separator(out, "Execution");
    if (report->parallel) {
        fprintf(out, "Requested threads: %d\n", report->parallel->requested_threads);
        fprintf(out, "Effective workers: %d\n", report->parallel->effective_workers);
        fprintf(out, "Mode:              %s\n", report->parallel->mode);
    }
    fprintf(out, "Benchmark:         %s\n", report->benchmark ? "yes" : "no");
    fprintf(out, "Type detection:    numeric if at least 70%% of non-missing values are finite\n");
    {
        int any_rule = 0;
        for (column = 0; column < report->schema->column_count; column++) {
            const RangeRule *rule = &report->schema->rules[column];
            if (!rule->active) {
                continue;
            }
            any_rule = 1;
            fprintf(out, "Range rule:        %s in [%.4g, %.4g]\n",
                    report->dataset->headers[column], rule->min_value, rule->max_value);
        }
        if (!any_rule) {
            fprintf(out, "Range rules:       none\n");
        }
    }

    print_separator(out, "Quality summary");
    fprintf(out, "Valid records processed: %zu\n", report->analysis->records);
    fprintf(out, "Malformed records skipped: %zu\n", report->dataset->malformed_count);
    fprintf(out, "Incomplete records:      %zu\n", report->analysis->incomplete_records);
    fprintf(out, "Missing cells:           %zu\n", report->analysis->missing_cells);
    fprintf(out, "An incomplete record has at least one empty or whitespace-only cell.\n");
    fprintf(out, "It is counted once. Invalid text in a numeric column is a separate category.\n");

    print_separator(out, "Column analysis");
    fprintf(out, "%-20s %-8s %8s %8s %8s %8s %8s %12s %12s %12s %10s\n",
            "Column", "Type", "Num%", "Missing", "Miss%", "Valid", "Invalid",
            "Min", "Max", "Mean", "OutOfRange");
    fprintf(out, "%-20s %-8s %8s %8s %8s %8s %8s %12s %12s %12s %10s\n",
            "--------------------", "--------", "--------", "--------", "--------",
            "--------", "--------", "------------", "------------", "------------",
            "----------");

    for (column = 0; column < report->dataset->column_count; column++) {
        const ColumnInfo *info = &report->schema->columns[column];
        const ColumnStats *stats = &report->analysis->columns[column];
        const RangeRule *rule = &report->schema->rules[column];
        char label[21];
        char numeric_share[16];
        char missing_pct[16];
        char valid_buf[16];
        char invalid_buf[16];
        char min_buf[24];
        char max_buf[24];
        char mean_buf[24];
        char range_buf[16];

        truncate_label(report->dataset->headers[column], label, sizeof label);
        if (info->non_missing == 0) {
            snprintf(numeric_share, sizeof numeric_share, "n/a");
        } else {
            snprintf(numeric_share, sizeof numeric_share, "%.1f%%",
                     100.0 * (double)info->numeric_parsed / (double)info->non_missing);
        }
        if (report->analysis->records == 0) {
            snprintf(missing_pct, sizeof missing_pct, "n/a");
        } else {
            snprintf(missing_pct, sizeof missing_pct, "%.2f%%",
                     100.0 * (double)stats->missing / (double)report->analysis->records);
        }

        if (info->kind == COL_NUMERIC) {
            snprintf(valid_buf, sizeof valid_buf, "%zu", stats->valid_numeric);
            snprintf(invalid_buf, sizeof invalid_buf, "%zu", stats->invalid);
            if (stats->valid_numeric == 0) {
                snprintf(min_buf, sizeof min_buf, "-");
                snprintf(max_buf, sizeof max_buf, "-");
                snprintf(mean_buf, sizeof mean_buf, "-");
            } else {
                snprintf(min_buf, sizeof min_buf, "%.4f", stats->min_value);
                snprintf(max_buf, sizeof max_buf, "%.4f", stats->max_value);
                snprintf(mean_buf, sizeof mean_buf, "%.4f", column_mean(stats));
            }
            if (rule->active) {
                snprintf(range_buf, sizeof range_buf, "%zu", stats->out_of_range);
            } else {
                snprintf(range_buf, sizeof range_buf, "-");
            }
        } else {
            snprintf(valid_buf, sizeof valid_buf, "-");
            snprintf(invalid_buf, sizeof invalid_buf, "-");
            snprintf(min_buf, sizeof min_buf, "-");
            snprintf(max_buf, sizeof max_buf, "-");
            snprintf(mean_buf, sizeof mean_buf, "-");
            snprintf(range_buf, sizeof range_buf, "-");
        }

        fprintf(out, "%-20s %-8s %8s %8zu %8s %8s %8s %12s %12s %12s %10s\n",
                label, info->kind == COL_NUMERIC ? "numeric" : "text",
                numeric_share, stats->missing, missing_pct,
                valid_buf, invalid_buf, min_buf, max_buf, mean_buf, range_buf);
    }
    fprintf(out, "Num%% is the share of non-missing values that parsed as finite numbers.\n");
    fprintf(out, "Display names longer than 20 characters are shortened; matching uses the full name.\n");

    print_separator(out, "Range violations");
    {
        int any_rule = 0;
        int any_violation = 0;
        for (column = 0; column < report->schema->column_count; column++) {
            const RangeRule *rule = &report->schema->rules[column];
            if (!rule->active) {
                continue;
            }
            any_rule = 1;
            if (report->analysis->columns[column].out_of_range > 0) {
                any_violation = 1;
            }
            fprintf(out, "%-20s [%.4g, %.4g]  out of range: %zu\n",
                    report->dataset->headers[column], rule->min_value, rule->max_value,
                    report->analysis->columns[column].out_of_range);
        }
        if (!any_rule) {
            fprintf(out, "No range rules were configured.\n");
        } else if (!any_violation) {
            fprintf(out, "Configured rules matched every valid numeric value.\n");
        }
        fprintf(out, "Only finite numeric values are checked. Missing and invalid cells are not out of range.\n");
        fprintf(out, "Bounds are inclusive.\n");
    }

    if (report->benchmark) {
        print_separator(out, "Benchmark");
        format_ms(serial_time, sizeof serial_time, report->benchmark->serial_ms);
        format_ms(parallel_time, sizeof parallel_time, report->benchmark->parallel_ms);
        fprintf(out, "Single-threaded time:   %s\n", serial_time);
        fprintf(out, "Multithreaded time:     %s\n", parallel_time);
        if (report->parallel) {
            fprintf(out, "Requested threads:      %d\n", report->parallel->requested_threads);
            fprintf(out, "Effective workers:      %d\n", report->parallel->effective_workers);
        }
        if (report->benchmark->speedup_valid) {
            fprintf(out, "Speedup:                %.3fx\n", report->benchmark->speedup);
            if (report->benchmark->comparison > 0) {
                fprintf(out, "Comparison:             multithreading was faster for this workload\n");
            } else if (report->benchmark->comparison < 0) {
                fprintf(out, "Comparison:             multithreading was slower for this workload\n");
            } else {
                fprintf(out, "Comparison:             the two times were effectively the same\n");
            }
        } else {
            fprintf(out, "Speedup:                n/a\n");
            fprintf(out, "Comparison:             elapsed time is too small to compare reliably\n");
        }
        fprintf(out, "Result check:           %s\n",
                report->benchmark->equivalent ? "passed" : "FAILED");
        fprintf(out, "Detail:                 %s\n", report->benchmark->detail);
        fprintf(out, "Timing excludes CSV parsing and column-type detection.\n");
        fprintf(out, "Timing includes worker creation, joining, and merging thread-local results.\n");
        fprintf(out, "Speedup is single-threaded time divided by multithreaded time.\n");
    }

    if (report->warning_count > 0) {
        size_t warning;
        print_separator(out, "Warnings");
        for (warning = 0; warning < report->warning_count; warning++) {
            fprintf(out, "- %s\n", report->warnings[warning]);
        }
    }
}
