# DataSentry — Parallel Dataset Quality Analyzer

DataSentry is a command-line program, written in C, that reads a CSV file and reports data-quality problems in its columns. It detects numeric columns, summarizes them, counts missing and invalid values, and checks inclusive numeric ranges. The same analysis runs once on one thread and once on a set of POSIX threads so the two results, and their running times, can be compared.

The project is sized for a second- or third-year Operating Systems practical. The difficult part is real thread coordination, not the statistics.

## Problem and objectives

A dataset can look complete and still contain blank cells, text where a number was expected, and numbers outside a meaningful range. DataSentry makes those problems visible for a CSV file.

The program is also a small study of data parallelism:

- Split an already loaded table into disjoint row ranges.
- Let each worker thread compute private counters.
- Join the workers, then merge those counters.
- Compare that result with a single-threaded run of the same logic.

## Operating systems concepts

The process loads the file, then creates worker threads with `pthread_create`. Threads share the dataset. That is different from creating several processes, which would each need their own copy or an explicit shared-memory region.

During analysis the dataset and column schema are read-only. Each worker writes a separate `AnalysisResult`. Workers therefore do not update the same counters, and the analysis does not take a lock. A mutex around the whole scan would force the threads to take turns and would remove the point of running them together.

`pthread_join` is the barrier before the merge. The main thread does not read a worker's counters until that worker has finished. The merge itself is single-threaded.

Rows are divided into contiguous ranges of nearly equal length. If the row count is not divisible by the worker count, the first few workers receive one extra row. Ranges do not overlap and, together, they cover every valid record. The program never starts more workers than there are valid records, and it never starts more than 64.

If `pthread_create` fails after some workers have started, those workers are joined and the remaining ranges are analyzed on the main thread. The report says that this fallback happened. The statistics still include every valid record.

Speedup is `serial time / parallel time`. The parallel measurement includes creating the threads, joining them, and merging their results. It does not include reading the CSV or deciding which columns are numeric. On a tiny file, creating threads can take longer than the scan. DataSentry reports that outcome when it happens. It does not add artificial delays.

## Architecture

```
CSV file
  |  csv_load          read the file, keep valid rows, count malformed rows
  v
Dataset                owned strings, read-only after this point
  |  schema_build      70% numeric test, before the timer starts
  v
Schema + range rules
  |  analyze_serial    one call over every valid row
  |  analyze_parallel  partition, pthread_create, private stats, join, merge
  v
Compare counts and floating-point summaries
  |
  v
Text report
```

`analyze_range` is the only row scan. The serial function calls it on the full table. Each worker calls it on its own slice. The benchmark therefore compares two executions of the same classification rules.

## CSV parsing

The parser accepts a header row, comma-separated fields, quotes around a field, a comma inside quotes, and a doubled quote (`""`) as an escaped quote. It accepts both LF and CRLF line endings. A blank cell, or a cell that contains only spaces and tabs, is stored and later counted as missing. A blank line in the file is a record. One trailing newline after the last record does not create an extra record.

A data row whose field count differs from the header, or whose quotes do not close on that same line, is malformed. It is counted, discarded, and the parser continues. A missing cell in a row that has the right number of fields is not malformed.

Quoted fields cannot span lines. A field is quoted only when its first character is `"`. Column names are trimmed. Duplicate or empty header names are rejected. A leading UTF-8 byte-order mark is ignored.

## Numeric columns and quality counts

A non-missing value is numeric when `strtod` consumes the whole token after surrounding whitespace is ignored, the result is finite, and the conversion does not overflow or underflow. `nan`, `inf`, and leftover characters such as `12abc` are rejected. Scientific notation such as `1e2` is accepted.

A column is numeric when it has at least one finite value and at least 70% of its non-missing values are finite. The test is the integer comparison `numeric_count * 10 >= non_missing * 7`, so 7 of 10 values qualify and 6 of 10 do not. This rule is a heuristic. A column of mostly numbers with a few labels is numeric. A column of identifiers that happen to contain a few numbers stays text. The `Num%` column in the report is the share of non-missing values that parsed as numbers, which is the evidence for the decision.

For a numeric column the report shows how many values were finite, how many were non-missing but not numeric, the minimum, maximum, and arithmetic mean, the missing count and percentage, and the out-of-range count when a rule is configured. Text columns report their type and missing values only.

Counts use structurally valid records only:

- **Data records** are every row after the header, including malformed rows.
- **Valid records** have the same number of fields as the header.
- **Malformed records** are skipped.
- **Missing cell** is an empty or whitespace-only field in a valid record.
- **Incomplete record** has one or more missing cells and is counted once, even if several cells are blank.
- **Invalid value** is a non-missing cell in a numeric column that is not a finite number.
- **Out of range** applies only to a finite number, and only when that column has a rule. The limits are inclusive. Missing and invalid cells are not out of range.

The total missing-cell count equals the sum of the per-column missing counts. For a numeric column, `valid + invalid + missing` equals the number of valid records.

## Ranges, threads, and the benchmark

A range argument has the form `Column:min:max`. The minimum must be finite and strictly less than the maximum, so a rule such as `Age:10:10` is rejected. Repeat `--range` for several columns. Quote the argument when the name contains spaces: `--range "Full Name:0:10"`. Names are matched exactly, after the header is trimmed. A rule for an unknown column, a text column, or a column that already has a rule is an error.

The default worker count is 4. `--threads` accepts an integer from 1 through 64. One worker is still a real `pthread`. If the file has fewer valid rows than the requested count, the extra threads are not created and the report says why.

Both timed runs use the dataset already in memory, the same column types, and the same range rules. After the runs, integer counters must match exactly. Minimum, maximum, sum, and mean must match within a relative tolerance of `1e-9`. A mismatch is printed and the process exits with status 1. The summary then shows the serial result.

Very small pairs of measurements, where both times are under 0.001 ms, do not produce a speedup ratio.

The program sets the C locale before parsing or starting threads, so the decimal point is `.` and `strtod` is not depending on a locale change while workers run.

## Build and run

Requirements: a C11 compiler with POSIX threads (Clang on macOS or GCC on Linux). There are no extra libraries. `make` passes `-pthread`.

```sh
make
make test
make run
make clean
```

`make` builds `./datasentry` with:

```text
-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -O2 -pthread
```

`make run` is:

```sh
./datasentry data/sample.csv --threads 4 --benchmark \
    --range "Age:0:120" --range "Attendance:0:100" --range "Score:0:100"
```

Other examples:

```sh
./datasentry data/sample.csv
./datasentry data/sample.csv --threads 4
./datasentry --help
```

Run these commands from the project directory. `--help` exits 0. Invalid options, a bad thread count, a missing file, a range that cannot be applied, and a benchmark mismatch exit 1.

## Sample dataset

`data/sample.csv` has the columns `StudentID`, `Name`, `Age`, `Attendance`, `Score`, and `Email`. The people and addresses are synthetic. The file has 42 data rows: 40 valid rows and 2 malformed rows (`S041` has too few fields, `S042` has too many).

Issues placed in the valid rows:

| Row | What is wrong |
| --- | --- |
| S002 | Name is quoted and contains a comma |
| S023 | Name contains an escaped quote |
| S024 | Score is numeric but surrounded by spaces |
| S025–S029 | Age 120, attendance 100 and 0, score 100, and age 0, all inside the inclusive limits |
| S030 | Age is missing |
| S031 | Age is `unknown` |
| S032, S033 | Age is 150 and -2, outside 0..120 |
| S034 | Attendance is 142, outside 0..100 |
| S035 | Attendance is `high` |
| S036 | Score is missing |
| S037 | Score is `excellent` |
| S038 | Score is 105, outside 0..100 |
| S039 | Email is missing |
| S040 | Attendance and score are both missing, so this is one incomplete record with two missing cells |

For the ranges `Age:0:120`, `Attendance:0:100`, and `Score:0:100`, the program reports 4 incomplete records, 5 missing cells, 2 ages out of range, 1 attendance out of range, and 1 score out of range. The boundary values listed above are not counted as violations.

## Measured results

These commands were run on 15 October 2026 on Darwin 25.5.0 (arm64) with Apple Clang 21.0.0, using the `-O2` binary from `make`. Quality counts for `data/sample.csv` are properties of that file. The times are from the runs below and will differ on another machine.

`make test` completed with **234 checks passed, 0 failed**.

The same tests were compiled again with `-fsanitize=address,undefined` and also finished with 234 checks passed and no sanitizer report. Linux GCC was not available in this environment. The Makefile uses the same C11 and POSIX flags there.

One `make run` produced this transcript:

```text
DataSentry — Parallel Dataset Quality Analyzer

== Dataset ==
File:              data/sample.csv
Columns:           6
Column names:      StudentID, Name, Age, Attendance, Score, Email
Data records:      42
Valid records:     40
Malformed records: 2

== Execution ==
Requested threads: 4
Effective workers: 4
Mode:              multithreaded (4 workers)
Benchmark:         yes
Type detection:    numeric if at least 70% of non-missing values are finite
Range rule:        Age in [0, 120]
Range rule:        Attendance in [0, 100]
Range rule:        Score in [0, 100]

== Quality summary ==
Valid records processed: 40
Malformed records skipped: 2
Incomplete records:      4
Missing cells:           5
An incomplete record has at least one empty or whitespace-only cell.
It is counted once. Invalid text in a numeric column is a separate category.

== Column analysis ==
Column               Type         Num%  Missing    Miss%    Valid  Invalid          Min          Max         Mean OutOfRange
-------------------- -------- -------- -------- -------- -------- -------- ------------ ------------ ------------ ----------
StudentID            text         0.0%        0    0.00%        -        -            -            -            -          -
Name                 text         0.0%        0    0.00%        -        -            -            -            -          -
Age                  numeric     97.4%        1    2.50%       38        1      -2.0000     150.0000      25.3421          2
Attendance           numeric     97.4%        1    2.50%       38        1       0.0000     142.0000      86.8684          1
Score                numeric     97.4%        2    5.00%       37        1      60.0000     105.0000      77.3243          1
Email                text         0.0%        1    2.50%        -        -            -            -            -          -
Num% is the share of non-missing values that parsed as finite numbers.
Display names longer than 20 characters are shortened; matching uses the full name.

== Range violations ==
Age                  [0, 120]  out of range: 2
Attendance           [0, 100]  out of range: 1
Score                [0, 100]  out of range: 1
Only finite numeric values are checked. Missing and invalid cells are not out of range.
Bounds are inclusive.

== Benchmark ==
Single-threaded time:   0.003000 ms
Multithreaded time:     0.041 ms
Requested threads:      4
Effective workers:      4
Speedup:                0.073x
Comparison:             multithreading was slower for this workload
Result check:           passed
Detail:                 serial and parallel statistics match
Timing excludes CSV parsing and column-type detection.
Timing includes worker creation, joining, and merging thread-local results.
Speedup is single-threaded time divided by multithreaded time.
```

A second run of the same command on the sample measured 0.003000 ms serial and 0.048 ms parallel (0.062x). The result check passed again. Forty rows are not enough work to hide thread creation, so a speedup below 1 is the expected honest result here.

A separate 300,000-row file was generated only for timing and was not kept in the repository:

```sh
awk 'BEGIN {
  print "StudentID,Name,Age,Attendance,Score,Email"
  for (i = 1; i <= 300000; i++) {
    age = 18 + (i % 30); att = i % 101; score = 40 + (i % 80)
    printf "S%06d,Student %d,%d,%d,%d,s%d@example.com\n", i, i, age, att, score, i
  }
}' > /tmp/bench_large.csv

./datasentry /tmp/bench_large.csv --threads 4 --benchmark \
    --range "Age:0:120" --range "Attendance:0:100" --range "Score:0:100"
```

On that one run, serial analysis took 13.926 ms and 4 workers took 4.110 ms, a speedup of 3.388x. The result check passed. A follow-up run with `--threads 8` measured 15.001 ms serial and 4.524 ms parallel (3.316x). Eight workers were not faster than four on that run. These times exclude CSV parsing.

## Tests

`tests/test_core.c` is a plain C driver. It covers quoted commas, escaped quotes, CRLF, missing cells, incomplete records, malformed rows, the 70% boundary, invalid numbers, minimum, maximum and mean, inclusive ranges, serial and parallel agreement, one worker, more requested threads than rows, bad command-line options, and file-open failures. Temporary CSVs are created with `mkstemp` and removed. A few checks also execute `./datasentry`.

## Layout

```text
include/   csv.h, analyzer.h, benchmark.h, cli.h, report.h, util.h
src/       main.c, csv.c, analyzer.c, benchmark.c, cli.c, report.c, util.c
data/      sample.csv
tests/     test_core.c
Makefile
```

Each loaded string belongs to the dataset and is released by `dataset_free`. Analysis objects own their counter arrays and are released by `analysis_free`.

## Limitations

- Quoted fields must start and end on the same line.
- A quote is recognized only when `"` is the first character of the field.
- The 70% rule can misclassify an unusual column. `Num%` shows the evidence.
- A range cannot require a single exact value, because the minimum must be less than the maximum.
- Benchmark times depend on the CPU, compiler optimization, and how much work is in the file. The sample is large enough to show the quality rules and small enough that threading makes it slower.
- The fallback path for a failed `pthread_create` is implemented and reported, but the tests do not force that system call to fail.
- This tree was built and tested with Apple Clang on macOS. It was not executed with GCC on Linux here.
