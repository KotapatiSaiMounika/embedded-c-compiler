export default function TokenTable({ tokens }) {
  const shown = tokens.filter((t) => t.type !== "EOF");
  return (
    <div className="scroll">
      <table>
        <thead>
          <tr>
            <th>#</th>
            <th>Lexeme</th>
            <th>Token type</th>
            <th>Category</th>
            <th>Line:Col</th>
          </tr>
        </thead>
        <tbody>
          {shown.map((t, i) => (
            <tr key={i}>
              <td className="muted">{i + 1}</td>
              <td className="mono">{t.lexeme}</td>
              <td className="mono">{t.type}</td>
              <td>
                <span className={`chip ${t.category}`}>{t.category}</span>
              </td>
              <td className="muted">
                {t.line}:{t.column}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      {shown.length === 0 && <p className="muted">No tokens.</p>}
    </div>
  );
}