import { useRef } from "react";

// A textarea with a line-number gutter. Lines that have an error get a red number.
export default function Editor({ value, onChange, errorLines, onRun }) {
  const gutterRef = useRef(null);
  const lineCount = Math.max(1, value.split("\n").length);

  const handleKeyDown = (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
      e.preventDefault();
      onRun();
    }
  };

  return (
    <div className="editor">
      <div className="gutter" ref={gutterRef} aria-hidden="true">
        {Array.from({ length: lineCount }, (_, i) => (
          <div key={i} className={errorLines.has(i + 1) ? "gutter-line error" : "gutter-line"}>
            {i + 1}
          </div>
        ))}
      </div>
      <textarea
        className="code-input"
        value={value}
        spellCheck={false}
        wrap="off"
        onChange={(e) => onChange(e.target.value)}
        onKeyDown={handleKeyDown}
        onScroll={(e) => {
          if (gutterRef.current) gutterRef.current.scrollTop = e.target.scrollTop;
        }}
        aria-label="Source code"
      />
    </div>
  );
}