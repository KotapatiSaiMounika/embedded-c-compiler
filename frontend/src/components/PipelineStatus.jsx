const STAGES = [
  { key: "lexical", label: "Lexer" },
  { key: "syntax", label: "Parser" },
  { key: "semantic", label: "Semantic" },
  { key: "ir", label: "IR" },
  { key: "optimizer", label: "Optimizer" },
  { key: "codegen", label: "CodeGen" },
  { key: "runtime", label: "VM" },
];

// ok / failed / skipped for each stage, derived from the server's "stage" field.
function statuses(result) {
  if (result.stage === "ok") return STAGES.map(() => "ok");

  const failedAt = STAGES.findIndex((s) => s.key === result.stage);
  return STAGES.map((s, i) => {
    if (i === failedAt) return "failed";
    if (i > failedAt) return "skipped";
    // The VM checks (reserved names / shadowing) run before IR generation.
    if (result.stage === "codegen" && result.optimizedIr.length === 0 && (s.key === "ir" || s.key === "optimizer"))
      return "skipped";
    return "ok";
  });
}

export default function PipelineStatus({ result }) {
  const list = statuses(result);
  return (
    <div className="pipeline">
      {STAGES.map((s, i) => (
        <div key={s.key} className={`stage ${list[i]}`}>
          <span className="dot" />
          {s.label}
        </div>
      ))}
    </div>
  );
}