export default function BytecodeView({ bytecode }) {
  if (bytecode.length === 0) return <p className="muted">No bytecode was generated.</p>;
  const targets = new Set(
    bytecode.filter((b) => b.opcode === "JMP" || b.opcode === "JZ").map((b) => b.operand)
  );
  return (
    <div className="scroll">
      <table>
        <thead>
          <tr>
            <th>Addr</th>
            <th>Instruction</th>
            <th>Source line</th>
          </tr>
        </thead>
        <tbody>
          {bytecode.map((b) => (
            <tr key={b.address} className={targets.has(b.address) ? "jump-target" : ""}>
              <td className="muted">{b.address}</td>
              <td className="mono">{b.text}</td>
              <td className="muted">{b.line > 0 ? b.line : ""}</td>
            </tr>
          ))}
        </tbody>
      </table>
      <p className="muted small">Highlighted rows are jump targets.</p>
    </div>
  );
}