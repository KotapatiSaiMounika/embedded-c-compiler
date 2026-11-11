export default function Diagnostics({ diagnostics }) {
  if (!diagnostics || diagnostics.length === 0) return null;
  return (
    <div className="panel diagnostics">
      <h3>Diagnostics ({diagnostics.length})</h3>
      <ul>
        {diagnostics.map((d, i) => (
          <li key={i} className={`diag ${d.severity}`}>
            <span className="badge">{d.stage}</span>
            <span className="where">{d.line > 0 ? `line ${d.line}${d.column > 0 ? `:${d.column}` : ""}` : "—"}</span>
            <span className="msg">{d.message}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}