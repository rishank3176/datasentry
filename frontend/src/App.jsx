import { useMemo, useState } from "react";

const SAMPLE_RANGES = [
  { column: "Age", min: "0", max: "120" },
  { column: "Attendance", min: "0", max: "100" },
  { column: "Score", min: "0", max: "100" },
];

function blankRange() {
  return { column: "", min: "", max: "" };
}

function formatCount(value) {
  if (value == null) {
    return "—";
  }
  return Number(value).toLocaleString("en-US");
}

function formatNumber(value) {
  if (value == null || Number.isNaN(Number(value))) {
    return "—";
  }
  return Number(value).toLocaleString("en-US", { maximumFractionDigits: 4 });
}

function formatPercent(value) {
  if (value == null) {
    return "n/a";
  }
  return `${Number(value).toFixed(2)}%`;
}

function comparisonCopy(benchmark) {
  if (!benchmark) {
    return "";
  }
  if (benchmark.comparison === "faster") {
    return "Multithreading was faster for this workload.";
  }
  if (benchmark.comparison === "slower") {
    return "Multithreading was slower for this workload.";
  }
  if (benchmark.comparison === "too-small") {
    return "The elapsed time is too small to compare reliably.";
  }
  return "The two times were effectively the same.";
}

async function readError(response) {
  try {
    const body = await response.json();
    return body.error || "The analysis could not be completed.";
  } catch {
    return "The analysis could not be completed.";
  }
}

export default function App() {
  const [view, setView] = useState("analyze");
  const [file, setFile] = useState(null);
  const [threads, setThreads] = useState(4);
  const [benchmark, setBenchmark] = useState(true);
  const [ranges, setRanges] = useState(SAMPLE_RANGES);
  const [result, setResult] = useState(null);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  const activeRanges = useMemo(
    () => ranges.filter((range) => range.column.trim() && range.min !== "" && range.max !== ""),
    [ranges],
  );

  function updateRange(index, field, value) {
    setRanges((current) => current.map((range, item) => (
      item === index ? { ...range, [field]: value } : range
    )));
  }

  async function submit(event, useSample) {
    event.preventDefault();
    setError("");
    setLoading(true);
    try {
      const specs = activeRanges.map((range) => `${range.column.trim()}:${range.min}:${range.max}`);
      let response;
      if (useSample) {
        response = await fetch("/api/sample", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ threads: Number(threads), benchmark, ranges: specs }),
        });
      } else {
        if (!file) {
          setError("Choose a CSV file, or analyze the included sample.");
          setLoading(false);
          return;
        }
        const body = new FormData();
        body.append("file", file);
        body.append("threads", String(threads));
        body.append("benchmark", benchmark ? "true" : "false");
        specs.forEach((spec) => body.append("ranges", spec));
        response = await fetch("/api/analyze", { method: "POST", body });
      }
      if (!response.ok) {
        setError(await readError(response));
        return;
      }
      setResult(await response.json());
      setView("analyze");
    } catch {
      setError("The website could not reach the analysis server.");
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="page">
      <header className="topbar">
        <div className="brand">
          <span className="mark" aria-hidden="true" />
          <div>
            <p className="eyebrow">Operating systems practical</p>
            <strong>DataSentry</strong>
          </div>
        </div>
        <nav>
          <button type="button" className={view === "analyze" ? "active" : ""} onClick={() => setView("analyze")}>
            Analyze
          </button>
          <button type="button" className={view === "method" ? "active" : ""} onClick={() => setView("method")}>
            How it works
          </button>
        </nav>
      </header>

      {view === "method" ? <Method /> : (
        <main className="workspace">
          <form className="panel controls" onSubmit={(event) => submit(event, false)}>
            <div className="panel-head">
              <h1>Check a dataset</h1>
              <p>Upload a CSV. The C analyzer loads it, classifies columns, and compares one thread with a pool of workers.</p>
            </div>

            <label className="drop">
              <input
                type="file"
                accept=".csv,text/csv"
                onChange={(event) => setFile(event.target.files?.[0] ?? null)}
              />
              <span>{file ? file.name : "Choose a .csv file"}</span>
              <small>{file ? `${Math.ceil(file.size / 1024)} KB` : "Up to 20 MB. Header row required."}</small>
            </label>

            <label className="field">
              <span>Worker threads</span>
              <input
                type="number"
                min="1"
                max="64"
                value={threads}
                onChange={(event) => setThreads(event.target.value)}
              />
            </label>

            <label className="check">
              <input
                type="checkbox"
                checked={benchmark}
                onChange={(event) => setBenchmark(event.target.checked)}
              />
              <span>Compare single-threaded and multithreaded runs</span>
            </label>

            <div className="ranges">
              <div className="range-head">
                <h2>Numeric ranges</h2>
                <button type="button" className="text-button" onClick={() => setRanges((current) => [...current, blankRange()])}>
                  Add rule
                </button>
              </div>
              {ranges.map((range, index) => (
                <div className="range-row" key={index}>
                  <input
                    aria-label="Column name"
                    placeholder="Column"
                    value={range.column}
                    onChange={(event) => updateRange(index, "column", event.target.value)}
                  />
                  <input
                    aria-label="Minimum"
                    placeholder="Min"
                    value={range.min}
                    onChange={(event) => updateRange(index, "min", event.target.value)}
                  />
                  <input
                    aria-label="Maximum"
                    placeholder="Max"
                    value={range.max}
                    onChange={(event) => updateRange(index, "max", event.target.value)}
                  />
                  <button
                    type="button"
                    className="icon-button"
                    aria-label="Remove range"
                    onClick={() => setRanges((current) => current.filter((_, item) => item !== index))}
                  >
                    ×
                  </button>
                </div>
              ))}
              <p className="hint">Bounds are inclusive. A column is checked only when it is classified as numeric.</p>
            </div>

            {error ? <p className="error" role="alert">{error}</p> : null}

            <div className="actions">
              <button type="submit" className="primary" disabled={loading}>
                {loading ? "Analyzing…" : "Analyze upload"}
              </button>
              <button type="button" className="secondary" disabled={loading} onClick={(event) => submit(event, true)}>
                Analyze sample
              </button>
            </div>
          </form>

          <section className="panel results" aria-live="polite">
            {result ? <Report result={result} /> : <EmptyState />}
          </section>
        </main>
      )}
    </div>
  );
}

function EmptyState() {
  return (
    <div className="empty">
      <p className="eyebrow">Waiting for a dataset</p>
      <h2>Missing, invalid, and out-of-range values show up here.</h2>
      <p>
        The included sample has blank scores, an age of 150, attendance above 100, and two malformed rows.
        Analyze it to see the quality table and the thread benchmark.
      </p>
    </div>
  );
}

function Report({ result }) {
  const benchmark = result.benchmark;
  const widest = benchmark
    ? Math.max(benchmark.serialMs || 0, benchmark.parallelMs || 0, 0.000001)
    : 1;

  return (
    <div className="report">
      <div className="report-title">
        <div>
          <p className="eyebrow">{result.file}</p>
          <h2>{result.execution?.mode || "Analysis complete"}</h2>
        </div>
        {result.exitCode ? <span className="pill warn">Check failed</span> : <span className="pill ok">Report ready</span>}
      </div>

      <div className="stats">
        <Stat label="Data records" value={formatCount(result.dataset.dataRecords)} />
        <Stat label="Valid records" value={formatCount(result.dataset.validRecords)} />
        <Stat label="Malformed" value={formatCount(result.dataset.malformedRecords)} />
        <Stat label="Incomplete" value={formatCount(result.quality.incompleteRecords)} />
        <Stat label="Missing cells" value={formatCount(result.quality.missingCells)} />
      </div>

      <div className="meta">
        <span>{result.execution.requestedThreads} requested</span>
        <span>{result.execution.effectiveWorkers} workers</span>
        <span>{result.dataset.columnCount} columns</span>
      </div>

      {result.warnings?.length ? (
        <ul className="warnings">
          {result.warnings.map((warning) => <li key={warning}>{warning}</li>)}
        </ul>
      ) : null}

      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Column</th>
              <th>Type</th>
              <th>Num%</th>
              <th>Missing</th>
              <th>Valid</th>
              <th>Invalid</th>
              <th>Min</th>
              <th>Max</th>
              <th>Mean</th>
              <th>Out of range</th>
            </tr>
          </thead>
          <tbody>
            {result.columns.map((column) => (
              <tr key={column.name}>
                <td>{column.name}</td>
                <td>{column.type}</td>
                <td>{column.numericShare == null ? "n/a" : `${Number(column.numericShare).toFixed(1)}%`}</td>
                <td>{formatCount(column.missing)} <small>{formatPercent(column.missingPercent)}</small></td>
                <td>{formatCount(column.valid)}</td>
                <td>{formatCount(column.invalid)}</td>
                <td>{formatNumber(column.min)}</td>
                <td>{formatNumber(column.max)}</td>
                <td>{formatNumber(column.mean)}</td>
                <td>{formatCount(column.outOfRange)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div className="split">
        <article>
          <h3>Range violations</h3>
          {result.ranges?.length ? (
            <ul className="range-list">
              {result.ranges.map((range) => {
                const column = result.columns.find((item) => item.name === range.column);
                return (
                  <li key={range.column}>
                    <strong>{range.column}</strong>
                    <span>[{formatNumber(range.min)}, {formatNumber(range.max)}]</span>
                    <em>{formatCount(column?.outOfRange)} outside</em>
                  </li>
                );
              })}
            </ul>
          ) : <p>No range rules were applied.</p>}
        </article>

        <article>
          <h3>Benchmark</h3>
          {benchmark ? (
            <>
              <Bar label="Single thread" ms={benchmark.serialMs} width={(benchmark.serialMs / widest) * 100} />
              <Bar label={`${result.execution.effectiveWorkers} workers`} ms={benchmark.parallelMs} width={(benchmark.parallelMs / widest) * 100} />
              <p className="verdict">
                {benchmark.speedup == null ? "Speedup n/a" : `${Number(benchmark.speedup).toFixed(3)}x speedup`}
                {" · "}
                {comparisonCopy(benchmark)}
                {" "}
                {benchmark.equivalent ? "Serial and parallel statistics match." : "Serial and parallel statistics do not match."}
              </p>
            </>
          ) : <p>Turn on the comparison to time both execution modes.</p>}
        </article>
      </div>
    </div>
  );
}

function Stat({ label, value }) {
  return (
    <div className="stat">
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  );
}

function Bar({ label, ms, width }) {
  return (
    <div className="bar-row">
      <div className="bar-label">
        <span>{label}</span>
        <span>{Number(ms).toFixed(3)} ms</span>
      </div>
      <div className="track">
        <div className="fill" style={{ width: `${Math.max(width, 2)}%` }} />
      </div>
    </div>
  );
}

function Method() {
  return (
    <main className="method">
      <section>
        <p className="eyebrow">What the website is doing</p>
        <h1>The page is a front door. The analysis still runs in C.</h1>
        <p>
          A request sends the CSV to the Node server. That server starts the <code>datasentry</code> program
          with POSIX threads, reads its JSON report, and returns it to this page. Parsing and statistics are
          not reimplemented in JavaScript.
        </p>
      </section>
      <div className="cards">
        <article>
          <h2>Shared table, private counters</h2>
          <p>Workers read the same parsed rows. Each one writes only its own totals. The main thread merges those totals after every worker has been joined.</p>
        </article>
        <article>
          <h2>Disjoint row ranges</h2>
          <p>Valid records are split into contiguous slices. The program does not start more workers than there are rows, and it caps the pool at 64.</p>
        </article>
        <article>
          <h2>Same work, two clocks</h2>
          <p>The benchmark runs the identical scan once in serial and once with the requested workers. Thread creation, joining, and merging are inside the parallel time.</p>
        </article>
        <article>
          <h2>How a cell is classified</h2>
          <p>A blank cell is missing. Text in a numeric column is invalid. A finite number outside an inclusive range is out of range. A row with the wrong number of fields is malformed and skipped.</p>
        </article>
      </div>
    </main>
  );
}
