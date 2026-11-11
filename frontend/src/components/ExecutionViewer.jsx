import { useEffect, useRef, useState } from "react";

export default function ExecutionViewer({ execution, bytecode }) {
  const trace = execution.trace;
  const last = trace.length - 1;
  const [index, setIndex] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [delay, setDelay] = useState(300);
  const activeRow = useRef(null);

  // New result -> start from the beginning.
  useEffect(() => {
    setIndex(0);
    setPlaying(false);
  }, [execution]);

  // Auto-play.
  useEffect(() => {
    if (!playing) return undefined;
    if (index >= last) {
      setPlaying(false);
      return undefined;
    }
    const timer = setTimeout(() => setIndex((i) => Math.min(i + 1, last)), delay);
    return () => clearTimeout(timer);
  }, [playing, index, last, delay]);

  // Keep the executing instruction visible.
  useEffect(() => {
    if (activeRow.current) activeRow.current.scrollIntoView({ block: "nearest" });
  }, [index]);

  const step = trace[index];
  const previous = index > 0 ? trace[index - 1] : null;
  const atEnd = index === last;
  const stack = step.stack;

  return (
    <div className="exec">
      <div className="controls">
        <button onClick={() => { setPlaying(false); setIndex(0); }} disabled={index === 0}>⏮ Reset</button>
        <button onClick={() => { setPlaying(false); setIndex((i) => Math.max(0, i - 1)); }} disabled={index === 0}>◀ Back</button>
        <button onClick={() => { setPlaying(false); setIndex((i) => Math.min(last, i + 1)); }} disabled={atEnd}>Step ▶</button>
        <button className="primary" onClick={() => setPlaying((p) => !p)} disabled={atEnd && !playing}>
          {playing ? "⏸ Pause" : "▶ Play"}
        </button>
        <button onClick={() => { setPlaying(false); setIndex(last); }} disabled={atEnd}>⏭ End</button>
        <label className="speed">
          Speed
          <select value={delay} onChange={(e) => setDelay(Number(e.target.value))}>
            <option value={800}>Slow</option>
            <option value={300}>Normal</option>
            <option value={80}>Fast</option>
            <option value={15}>Very fast</option>
          </select>
        </label>
      </div>

      <input
        className="slider"
        type="range"
        min="0"
        max={last}
        value={index}
        onChange={(e) => { setPlaying(false); setIndex(Number(e.target.value)); }}
        aria-label="Execution step"
      />
      <p className="muted small">
        Step {step.step} of {execution.steps}
        {execution.traceTruncated ? ` (trace limited to the first ${trace.length} steps)` : ""}
        {" · "}
        {step.step === 0 ? "program not started" : <>executed <span className="mono">{step.instruction}</span>{step.line > 0 ? ` (source line ${step.line})` : ""}</>}
      </p>

      {step.error && <div className="alert error">Runtime error: {step.error}</div>}
      {atEnd && !step.error && !execution.traceTruncated && execution.status === "halted" && (
        <div className="alert ok">Program finished (HALT) after {execution.steps} steps.</div>
      )}

      <div className="exec-grid">
        <div className="panel inner">
          <h4>Bytecode</h4>
          <div className="code-list">
            {bytecode.map((b) => {
              const isActive = b.address === step.pc;
              const isNext = b.address === step.nextPc && !atEnd;
              return (
                <div
                  key={b.address}
                  ref={isActive ? activeRow : null}
                  className={`code-row${isActive ? " active" : ""}${isNext ? " next" : ""}`}
                >
                  <span className="addr">{b.address}</span>
                  <span className="mono">{b.text}</span>
                  <span className="marker">{isActive ? "executed" : isNext ? "next" : ""}</span>
                </div>
              );
            })}
          </div>
        </div>

        <div className="side">
          <div className="panel inner">
            <h4>Stack <span className="muted">(top is highest)</span></h4>
            <div className="stack">
              {stack.length === 0 && <span className="muted">empty</span>}
              {[...stack].reverse().map((v, i) => (
                <div key={i} className={`stack-cell${i === 0 ? " top" : ""}`}>{v}</div>
              ))}
            </div>
          </div>

          <div className="panel inner">
            <h4>Memory (variables)</h4>
            {execution.variables.length === 0 ? (
              <p className="muted">No variables.</p>
            ) : (
              <table>
                <thead>
                  <tr><th>Name</th><th>Value</th></tr>
                </thead>
                <tbody>
                  {execution.variables.map((name, i) => {
                    const changed = previous && previous.memory[i] !== step.memory[i];
                    return (
                      <tr key={name} className={changed ? "changed" : ""}>
                        <td className="mono">{name}</td>
                        <td className="mono">{step.memory[i]}</td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            )}
          </div>
        </div>
      </div>

      {execution.traceTruncated && (
        <p className="muted small">
          Final values after all {execution.steps} steps:{" "}
          {execution.variables.map((n, i) => `${n} = ${execution.finalMemory[i]}`).join(", ")}
        </p>
      )}
    </div>
  );
}