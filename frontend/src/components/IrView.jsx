function Listing({ title, items }) {
  return (
    <div className="ir-col">
      <h4>
        {title} <span className="muted">({items.length})</span>
      </h4>
      <ol className="listing">
        {items.map((ins) => (
          <li key={ins.index} className={ins.op === "label" ? "label-line" : ""}>
            <span className="mono">{ins.text}</span>
          </li>
        ))}
      </ol>
    </div>
  );
}

export default function IrView({ ir, optimizedIr }) {
  if (ir.length === 0) return <p className="muted">No intermediate code was generated.</p>;
  const removed = ir.length - optimizedIr.length;
  return (
    <div>
      <p className="muted">
        Three-address code. The optimizer folds constants, propagates them and removes dead temporaries
        {removed > 0 ? ` — it removed ${removed} instruction${removed === 1 ? "" : "s"} here.` : "."}
      </p>
      <div className="ir-grid">
        <Listing title="Before optimization" items={ir} />
        <Listing title="After optimization" items={optimizedIr} />
      </div>
    </div>
  );
}