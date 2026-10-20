# DataSentry — build, test, and sample run.
# cc is Clang on macOS and typically GCC on Linux. Override with `make CC=gcc`.

CC ?= cc
CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -O2 -pthread
CPPFLAGS := -Iinclude
LDLIBS := -pthread

SRC := src/util.c src/csv.c src/analyzer.c src/benchmark.c src/cli.c src/report.c
HEADERS := include/util.h include/csv.h include/analyzer.h include/benchmark.h include/cli.h include/report.h

.PHONY: all test run clean

all: datasentry

datasentry: src/main.c $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/main.c $(SRC) $(LDLIBS)

datasentry_tests: tests/test_core.c $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/test_core.c $(SRC) $(LDLIBS)

test: datasentry datasentry_tests
	./datasentry_tests

run: datasentry
	./datasentry data/sample.csv --threads 4 --benchmark --range "Age:0:120" --range "Attendance:0:100" --range "Score:0:100"

clean:
	rm -f datasentry datasentry_tests
	rm -rf datasentry.dSYM datasentry_tests.dSYM
