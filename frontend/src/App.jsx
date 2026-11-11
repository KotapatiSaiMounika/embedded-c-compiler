import { useEffect, useMemo, useState } from "react";
import { checkHealth, compileSource } from "./api.js";
import { EXAMPLES } from "./examples.js";
import Editor from "./components/Editor.jsx";
import Diagnostics from "./components/Diagnostics.jsx";
import PipelineStatus from "./components/PipelineStatus.jsx";
import TokenTable from "./components/TokenTable.jsx";
import AstTree from "./components/AstTree.jsx";
import IrView from "./components/IrView.jsx";
import BytecodeView from "./components/BytecodeView.jsx";
import ExecutionViewer from "./components/ExecutionViewer.jsx";

const TABS = [
  { id: "execution", label: "Execution" },
  { id: "bytecode", label: "Bytecode" },
  { id: "ir", label: "IR" },
  { id: "ast", label: "AST" },
  { id: "tokens", label: "Tokens" },
];

export default function App() {
  const [source, setSource] = useState(EXAMPLES[0].code);
  const [result, setResult] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const [tab, setTab] = useState("execution");
  const [serverUp, setServerUp] = useState(null);

  useEffect(() => {
    checkHealth().then(setServerUp);
  }, []);

  const run = async () => {
    setLoading(true);
    setError("");
    try {
      const data = await compileSource(source);
      setResult(data);
      setServerUp(true);
      setTab(data.execution ? "execution" : data.ast ? "ast" : "tokens");
    } catch (e) {
      setError(e.message);
      setServerUp(false);
    } finally {
      setLoading(false);
    }
  };

  const errorLines = useMemo(() => {
    const lines = new Set();
    if (result) for (const d of result.diagnostics) if (d.severity === "error" && d.line > 0) lines.add(d.line);
    return lines;
  }, [result]);

  return (
    <div className="app">
      <header>
        <h1>Embedded C Subset Compiler</h1>
        <span className={`server ${serverUp === null ? "" : serverUp ? "up" : "down"}`}>
          {serverUp === null ? "checking server…" : serverUp ? "server online" : "server offline"}
        </span>
      </header>

      <main>
        <section className="left">
          <div className="toolbar">
            <select
              defaultValue=""
              onChange={(e) => {
                if (e.target.value !== "") setSource(EXAMPLES[Number(e.target.value)].code);
                e.target.value = "";
              }}
              aria-label="Load an example"
            >
              <option value="" disabled>Load example…</option>
              {EXAMPLES.map((ex, i) => (
                <option key={ex.name} value={i}>{ex.name}</option>
              ))}
            </select>
            <button className="primary" onClick={run} disabled={loading}>
              {loading ? "Compiling…" : "Compile & Run (Ctrl+Enter)"}
            </button>
          </div>

          <Editor value={source} onChange={setSource} errorLines={errorLines} onRun={run} />

          {error && <div className="alert error">{error}</div>}
          {result && <Diagnostics diagnostics={result.diagnostics} />}
        </section>

        <section className="right">
          {!result ? (
            <div className="panel placeholder">
              <h3>Ready</h3>
              <p>
                Write a program in the supported C subset (<span className="mono">int</span>,{" "}
                <span className="mono">if / else</span>, <span className="mono">while</span>, arithmetic,
                comparisons, <span className="mono">&amp;&amp; || !</span>) and press <b>Compile &amp; Run</b>.
              </p>
              <p className="muted">Then step through the virtual machine and watch the stack and memory change.</p>
            </div>
          ) : (
            <>
              <PipelineStatus result={result} />

              {result.execution && (
                <div className={`summary ${result.success ? "ok" : "fail"}`}>
                  {result.success ? "Finished" : "Stopped"} · {result.execution.steps} VM steps
                  {result.execution.variables.length > 0 && (
                    <span className="final">
                      {result.execution.variables.map((n, i) => (
                        <span key={n} className="var">
                          {n} = <b>{result.execution.finalMemory[i]}</b>
                        </span>
                      ))}
                    </span>
                  )}
                </div>
              )}
              {!result.execution && (
                <div className="summary fail">Compilation failed in the {result.stage} stage — see Diagnostics.</div>
              )}

              <nav className="tabs">
                {TABS.map((t) => (
                  <button key={t.id} className={tab === t.id ? "tab active" : "tab"} onClick={() => setTab(t.id)}>
                    {t.label}
                  </button>
                ))}
              </nav>

              <div className="panel tab-body">
                {tab === "execution" &&
                  (result.execution ? (
                    <ExecutionViewer execution={result.execution} bytecode={result.bytecode} />
                  ) : (
                    <p className="muted">The program was not executed because compilation failed.</p>
                  ))}
                {tab === "bytecode" && <BytecodeView bytecode={result.bytecode} />}
                {tab === "ir" && <IrView ir={result.ir} optimizedIr={result.optimizedIr} />}
                {tab === "ast" && <AstTree ast={result.ast} />}
                {tab === "tokens" && <TokenTable tokens={result.tokens} />}
              </div>
            </>
          )}
        </section>
      </main>
    </div>
  );
}