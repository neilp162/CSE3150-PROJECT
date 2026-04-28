"use client";

import { useEffect, useMemo, useRef, useState } from "react";

const demoData = {
  relationships: [
    "1|2|-1|demo",
    "1|3|0|demo",
    "3|4|-1|demo",
    "5|1|0|demo",
  ].join("\n"),
  announcements: [
    "seed_asn,prefix,rov_invalid",
    "2,203.0.113.0/24,False",
    "4,198.51.100.0/24,False",
    "2,192.0.2.0/24,True",
  ].join("\n"),
  rov: "3\n",
  target: 4,
};

function parseCsvLine(line) {
  const fields = [];
  let current = "";
  let quoted = false;

  for (let i = 0; i < line.length; i += 1) {
    const ch = line[i];
    if (ch === '"') {
      if (quoted && line[i + 1] === '"') {
        current += '"';
        i += 1;
      } else {
        quoted = !quoted;
      }
    } else if (ch === "," && !quoted) {
      fields.push(current.trim());
      current = "";
    } else {
      current += ch;
    }
  }

  fields.push(current.trim());
  return fields;
}

function parseResultCsv(csvText) {
  const lines = csvText.trim().split(/\r?\n/).filter(Boolean);
  if (lines.length <= 1) {
    return [];
  }

  return lines.slice(1).map((line) => {
    const [targetAsn, prefix, asPath, nextHop, receivedFrom, rovInvalid] =
      parseCsvLine(line);

    return {
      targetAsn,
      prefix,
      asPath,
      nextHop,
      receivedFrom,
      rovInvalid,
    };
  });
}

async function readFileText(file) {
  return file.text();
}

function UploadField({
  label,
  detail,
  fileName,
  buttonLabel,
  inputRef,
  accept,
  onChange,
}) {
  return (
    <section className="upload-field">
      <div className="field-head">
        <div>
          <span className="field-label">{label}</span>
          <small className="field-detail">{detail}</small>
        </div>
      </div>

      <input
        ref={inputRef}
        className="sr-only"
        type="file"
        accept={accept}
        onChange={onChange}
      />

      <div className="file-row">
        <button
          className="file-trigger"
          type="button"
          onClick={() => inputRef.current?.click()}
        >
          {buttonLabel}
        </button>
        <div className="file-summary">
          <strong>{fileName}</strong>
        </div>
      </div>
    </section>
  );
}

export default function Page() {
  const relInputRef = useRef(null);
  const annInputRef = useRef(null);
  const rovInputRef = useRef(null);
  const defaultInputsRef = useRef({
    relationships: null,
    rov: null,
  });

  const [wasmStatus, setWasmStatus] = useState("WASM loading");
  const [wasmReady, setWasmReady] = useState(false);
  const [message, setMessage] = useState({
    text: "Load inputs or use the demo data.",
    kind: "",
  });
  const [relationshipsFile, setRelationshipsFile] = useState(null);
  const [announcementsFile, setAnnouncementsFile] = useState(null);
  const [rovFile, setRovFile] = useState(null);
  const [targetAsn, setTargetAsn] = useState("");
  const [demoLoaded, setDemoLoaded] = useState(false);
  const [routeCount, setRouteCount] = useState("0");
  const [elapsedTime, setElapsedTime] = useState("0 ms");
  const [targetLabel, setTargetLabel] = useState("-");
  const [rows, setRows] = useState([]);
  const [lastCsv, setLastCsv] = useState("");
  const [simulateTarget, setSimulateTarget] = useState(null);
  const [freeResult, setFreeResult] = useState(null);
  const [mallocFn, setMallocFn] = useState(null);
  const [freeFn, setFreeFn] = useState(null);
  const [wasmModule, setWasmModule] = useState(null);

  const relationshipsName = useMemo(() => {
    if (demoLoaded) {
      return "Demo graph loaded";
    }
    return relationshipsFile
      ? relationshipsFile.name
      : "Using built-in CAIDA graph";
  }, [demoLoaded, relationshipsFile]);

  const announcementsName = useMemo(() => {
    if (demoLoaded) {
      return "Demo announcements loaded";
    }
    return announcementsFile
      ? announcementsFile.name
      : "Upload announcements CSV";
  }, [demoLoaded, announcementsFile]);

  const rovName = useMemo(() => {
    if (demoLoaded) {
      return "Demo ROV set loaded";
    }
    return rovFile ? rovFile.name : "Using built-in ROV set";
  }, [demoLoaded, rovFile]);

  const canRun =
    wasmReady &&
    targetAsn.trim() !== "" &&
    (demoLoaded || announcementsFile);

  useEffect(() => {
    let cancelled = false;

    async function initWasm() {
      try {
        const imported = await import(
          /* webpackIgnore: true */ "/simulator.js"
        );
        const createModule = imported.default;
        const module = await createModule({
          locateFile: (path) => `/${path}`,
        });

        if (cancelled) {
          return;
        }

        setWasmModule(module);
        setSimulateTarget(() =>
          module.cwrap("simulate_target", "number", [
            "number",
            "number",
            "number",
            "number",
          ])
        );
        setFreeResult(() => module.cwrap("free_result", null, ["number"]));
        setMallocFn(() => module.cwrap("malloc", "number", ["number"]));
        setFreeFn(() => module.cwrap("free", null, ["number"]));
        setWasmStatus("WASM ready");
        setWasmReady(true);
      } catch (error) {
        if (cancelled) {
          return;
        }
        setWasmStatus("WASM error");
        setMessage({
          text: error?.message || String(error),
          kind: "bad",
        });
      }
    }

    initWasm();
    return () => {
      cancelled = true;
    };
  }, []);

  function resetResults() {
    setRouteCount("0");
    setElapsedTime("0 ms");
    setTargetLabel("-");
    setRows([]);
    setLastCsv("");
  }

  function handleDemo() {
    setDemoLoaded(true);
    setTargetAsn(String(demoData.target));
    resetResults();
    setMessage({ text: "Demo data loaded.", kind: "good" });
  }

  function handleClear() {
    setDemoLoaded(false);
    setRelationshipsFile(null);
    setAnnouncementsFile(null);
    setRovFile(null);
    setTargetAsn("");
    resetResults();
    setMessage({ text: "Load inputs or use the demo data.", kind: "" });

    if (relInputRef.current) {
      relInputRef.current.value = "";
    }
    if (annInputRef.current) {
      annInputRef.current.value = "";
    }
    if (rovInputRef.current) {
      rovInputRef.current.value = "";
    }
  }

  async function handleSubmit(event) {
    event.preventDefault();
    if (!simulateTarget || !freeResult || !mallocFn || !freeFn || !wasmModule) {
      return;
    }

    const target = Number(targetAsn);
    if (!Number.isInteger(target) || target < 0) {
      setMessage({
        text: "Target ASN must be a non-negative integer.",
        kind: "bad",
      });
      return;
    }

    setMessage({ text: "Running simulation...", kind: "" });

    try {
      const started = performance.now();
      if (!defaultInputsRef.current.relationships) {
        defaultInputsRef.current.relationships = await fetch(
          "/default-relationships.txt"
        ).then((response) => {
          if (!response.ok) {
            throw new Error("Failed to load built-in CAIDA graph.");
          }
          return response.text();
        });
      }
      if (!defaultInputsRef.current.rov) {
        defaultInputsRef.current.rov = await fetch("/default-rov-asns.csv").then(
          (response) => {
            if (!response.ok) {
              throw new Error("Failed to load built-in ROV ASN file.");
            }
            return response.text();
          }
        );
      }

      const relationshipsText = demoLoaded
        ? demoData.relationships
        : relationshipsFile
          ? await readFileText(relationshipsFile)
          : defaultInputsRef.current.relationships;
      const announcementsText = demoLoaded
        ? demoData.announcements
        : await readFileText(announcementsFile);
      const rovText = demoLoaded
        ? demoData.rov
        : rovFile
          ? await readFileText(rovFile)
          : defaultInputsRef.current.rov;

      const allocations = [];
      const allocateUtf8 = (value) => {
        const size = wasmModule.lengthBytesUTF8(value) + 1;
        const pointer = mallocFn(size);
        wasmModule.stringToUTF8(value, pointer, size);
        allocations.push(pointer);
        return pointer;
      };

      const relationshipsPointer = allocateUtf8(relationshipsText);
      const announcementsPointer = allocateUtf8(announcementsText);
      const rovPointer = allocateUtf8(rovText);

      const pointer = simulateTarget(
        relationshipsPointer,
        announcementsPointer,
        rovPointer,
        target
      );
      const csvText = wasmModule.UTF8ToString(pointer);
      freeResult(pointer);
      for (const allocated of allocations) {
        freeFn(allocated);
      }

      if (csvText.startsWith("ERROR:")) {
        throw new Error(csvText.slice("ERROR:".length).trim());
      }

      const parsedRows = parseResultCsv(csvText);
      const elapsed = performance.now() - started;
      setRows(parsedRows);
      setLastCsv(csvText);
      setRouteCount(parsedRows.length.toLocaleString());
      setElapsedTime(`${Math.round(elapsed).toLocaleString()} ms`);
      setTargetLabel(String(target));
      setMessage({
        text: `Simulation complete for AS${target}.`,
        kind: "good",
      });
    } catch (error) {
      resetResults();
      setMessage({
        text: error?.message || String(error),
        kind: "bad",
      });
    }
  }

  function handleDownload() {
    if (!lastCsv) {
      return;
    }

    const blob = new Blob([lastCsv], { type: "text/csv" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = `target-as${targetAsn || "routes"}.csv`;
    document.body.append(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
  }

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand-block">
          <p className="eyebrow">CSE 3150 Extra Credit</p>
          <h1>BGP WASM Simulator</h1>
          <p className="intro-copy">
            Inspect the announcements visible at a single ASN with a
            browser-only WebAssembly run.
          </p>
          <div className="feature-strip" aria-label="Site capabilities">
            <span className="surface-badge">Client-side</span>
            <span className="surface-badge">WASM</span>
            <span className="surface-badge">CSV in / CSV out</span>
          </div>
        </div>

        <div className="status-cluster">
          <div
            className={`status-pill ${
              wasmReady ? "ready" : wasmStatus.includes("error") ? "error" : ""
            }`}
          >
            {wasmStatus}
          </div>
          <span className="status-caption">Custom domain live</span>
        </div>
      </header>

      <section className="workspace" aria-label="BGP simulator workspace">
        <form className="control-panel" onSubmit={handleSubmit}>
          <div className="panel-header">
            <div>
              <p className="panel-kicker">Inputs</p>
              <h2>Simulation setup</h2>
            </div>
          </div>

          <div className="field-grid">
            <UploadField
              label="Relationships"
              detail="Optional override"
              fileName={relationshipsName}
              buttonLabel="Choose file"
              inputRef={relInputRef}
              accept=".txt,.csv,text/plain"
              onChange={(event) => {
                setDemoLoaded(false);
                setRelationshipsFile(event.target.files?.[0] ?? null);
              }}
            />

            <UploadField
              label="Announcements"
              detail="Required"
              fileName={announcementsName}
              buttonLabel="Choose CSV"
              inputRef={annInputRef}
              accept=".csv,text/csv,text/plain"
              onChange={(event) => {
                setDemoLoaded(false);
                setAnnouncementsFile(event.target.files?.[0] ?? null);
              }}
            />

            <UploadField
              label="ROV ASNs"
              detail="Optional override"
              fileName={rovName}
              buttonLabel="Choose file"
              inputRef={rovInputRef}
              accept=".csv,.txt,text/csv,text/plain"
              onChange={(event) => {
                setDemoLoaded(false);
                setRovFile(event.target.files?.[0] ?? null);
              }}
            />

            <label className="number-field">
              <div className="field-head">
                <div>
                  <span className="field-label">Target ASN</span>
                  <small className="field-detail">Required</small>
                </div>
              </div>
              <input
                type="number"
                min="0"
                step="1"
                placeholder="4"
                value={targetAsn}
                onChange={(event) => setTargetAsn(event.target.value)}
              />
              <small className="field-detail">
                Routes visible at this AS
              </small>
            </label>
          </div>

          <p className="helper-note">
            Uploading announcements and a target ASN is enough. The app uses a
            built-in CAIDA graph snapshot and built-in ROV set unless you
            override them.
          </p>

          <div className="action-row">
            <button className="secondary" type="button" onClick={handleDemo}>
              <span aria-hidden="true">◎</span>
              Demo
            </button>
            <button className="primary" type="submit" disabled={!canRun}>
              <span aria-hidden="true">▶</span>
              Run
            </button>
            <button
              className="secondary"
              type="button"
              onClick={handleDownload}
              disabled={!lastCsv}
            >
              <span aria-hidden="true">↓</span>
              CSV
            </button>
            <button className="ghost" type="button" onClick={handleClear}>
              <span aria-hidden="true">×</span>
              Clear
            </button>
          </div>
        </form>

        <section className="output-panel" aria-live="polite">
          <div className="panel-header">
            <div>
              <p className="panel-kicker">Results</p>
              <h2>Routes at the selected ASN</h2>
            </div>
          </div>

          <div className="metric-strip">
            <div>
              <span className="metric-label">Routes</span>
              <strong>{routeCount}</strong>
            </div>
            <div>
              <span className="metric-label">Elapsed</span>
              <strong>{elapsedTime}</strong>
            </div>
            <div>
              <span className="metric-label">Target</span>
              <strong>{targetLabel}</strong>
            </div>
          </div>

          <div className={`message ${message.kind}`.trim()}>{message.text}</div>

          <div className="table-wrap">
            <table>
              <thead>
                <tr>
                  <th>Prefix</th>
                  <th>AS Path</th>
                  <th>Next Hop</th>
                  <th>Received</th>
                  <th>ROV Invalid</th>
                </tr>
              </thead>
              <tbody>
                {rows.length === 0 ? (
                  <tr>
                    <td colSpan="5" className="empty-cell">
                      No simulation results yet.
                    </td>
                  </tr>
                ) : (
                  rows.map((row) => (
                    <tr key={`${row.prefix}-${row.asPath}`}>
                      <td className="prefix-cell">{row.prefix}</td>
                      <td>{row.asPath}</td>
                      <td className="mono-cell">{row.nextHop}</td>
                      <td>
                        <span
                          className={`table-badge relationship-${row.receivedFrom}`}
                        >
                          {row.receivedFrom}
                        </span>
                      </td>
                      <td>
                        <span
                          className={`table-badge ${
                            row.rovInvalid === "true"
                              ? "invalid-true"
                              : "invalid-false"
                          }`}
                        >
                          {row.rovInvalid}
                        </span>
                      </td>
                    </tr>
                  ))
                )}
              </tbody>
            </table>
          </div>
        </section>
      </section>
    </main>
  );
}
