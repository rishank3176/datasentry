import express from "express";
import multer from "multer";
import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const binary = path.join(root, "datasentry");
const sampleCsv = path.join(root, "data", "sample.csv");
const frontendDist = path.join(root, "frontend", "dist");
const port = Number(process.env.PORT) || 8787;

const upload = multer({
  dest: os.tmpdir(),
  limits: { fileSize: 20 * 1024 * 1024, files: 1 },
});

function parseThreads(value) {
  if (value == null || value === "") {
    return 4;
  }
  if (!/^\d+$/.test(String(value))) {
    return null;
  }
  const threads = Number(value);
  if (!Number.isInteger(threads) || threads < 1 || threads > 64) {
    return null;
  }
  return threads;
}

function collectRanges(body) {
  const raw = body?.ranges;
  const list = raw == null ? [] : Array.isArray(raw) ? raw : [raw];
  const ranges = [];
  for (const item of list) {
    const range = String(item).trim();
    if (!range) {
      continue;
    }
    if (range.length > 200 || /[\u0000-\u001f]/.test(range)) {
      return { error: "A range rule is empty, too long, or contains a line break." };
    }
    ranges.push(range);
    if (ranges.length > 32) {
      return { error: "At most 32 range rules can be sent at once." };
    }
  }
  return { ranges };
}

function runAnalyzer(csvPath, { threads, benchmark, ranges }) {
  const args = [csvPath, "--json", "--threads", String(threads)];
  if (benchmark) {
    args.push("--benchmark");
  }
  for (const range of ranges) {
    args.push("--range", range);
  }

  return new Promise((resolve) => {
    const child = spawn(binary, args, { cwd: root });
    let stdout = "";
    let stderr = "";
    let settled = false;
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
    }, 60000);

    const finish = (result) => {
      if (settled) {
        return;
      }
      settled = true;
      clearTimeout(timer);
      resolve(result);
    };

    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) => {
      stdout += chunk;
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk;
    });
    child.on("error", (error) => {
      finish({ ok: false, error: error.message, stdout, stderr });
    });
    child.on("close", (code) => {
      let parsed = null;
      try {
        parsed = JSON.parse(stdout);
      } catch {
        parsed = null;
      }
      finish({ ok: parsed != null, code: code ?? 1, parsed, stderr: stderr.trim(), stdout });
    });
  });
}

async function analyzeFile(csvPath, displayName, options) {
  if (!fs.existsSync(binary)) {
    return {
      status: 500,
      body: { error: "The datasentry binary is missing. Run make in the project root first." },
    };
  }
  const result = await runAnalyzer(csvPath, options);
  if (!result.ok || !result.parsed) {
    const message = result.stderr || result.error || "The analyzer did not return a report.";
    return { status: 400, body: { error: message.replace(/^datasentry:\s*/gm, "").trim() } };
  }
  result.parsed.file = displayName;
  result.parsed.exitCode = result.code ?? 0;
  return { status: 200, body: result.parsed };
}

const app = express();
app.use(express.json({ limit: "1mb" }));

app.get("/api/health", (_req, res) => {
  res.json({
    ok: true,
    analyzer: fs.existsSync(binary),
  });
});

app.post("/api/sample", async (req, res) => {
  const threads = parseThreads(req.body?.threads);
  const collected = collectRanges(req.body);
  if (threads == null) {
    res.status(400).json({ error: "Thread count must be an integer from 1 to 64." });
    return;
  }
  if (collected.error) {
    res.status(400).json({ error: collected.error });
    return;
  }
  const outcome = await analyzeFile(sampleCsv, "sample.csv", {
    threads,
    benchmark: Boolean(req.body?.benchmark),
    ranges: collected.ranges,
  });
  res.status(outcome.status).json(outcome.body);
});

app.post("/api/analyze", upload.single("file"), async (req, res) => {
  const threads = parseThreads(req.body?.threads);
  const collected = collectRanges(req.body);
  if (!req.file) {
    res.status(400).json({ error: "Choose a CSV file to analyze." });
    return;
  }
  const uploaded = req.file.path;
  try {
    if (threads == null) {
      res.status(400).json({ error: "Thread count must be an integer from 1 to 64." });
      return;
    }
    if (collected.error) {
      res.status(400).json({ error: collected.error });
      return;
    }
    const name = req.file.originalname || "upload.csv";
    if (!name.toLowerCase().endsWith(".csv")) {
      res.status(400).json({ error: "Upload a file that ends in .csv." });
      return;
    }
    const outcome = await analyzeFile(uploaded, path.basename(name), {
      threads,
      benchmark: req.body?.benchmark === "true" || req.body?.benchmark === "1",
      ranges: collected.ranges,
    });
    res.status(outcome.status).json(outcome.body);
  } finally {
    fs.unlink(uploaded, () => {});
  }
});

app.use((error, _req, res, next) => {
  if (error instanceof multer.MulterError && error.code === "LIMIT_FILE_SIZE") {
    res.status(400).json({ error: "That CSV is larger than the 20 MB upload limit." });
    return;
  }
  if (error) {
    res.status(400).json({ error: error.message || "Upload failed." });
    return;
  }
  next();
});

if (fs.existsSync(frontendDist)) {
  app.use(express.static(frontendDist));
  app.get(/^(?!\/api).*/, (_req, res) => {
    res.sendFile(path.join(frontendDist, "index.html"));
  });
}

app.listen(port, "127.0.0.1", () => {
  console.log(`DataSentry API listening on http://127.0.0.1:${port}`);
});
