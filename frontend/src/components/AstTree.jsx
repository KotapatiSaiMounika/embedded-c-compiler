import { useState } from "react";

function Node({ node, depth }) {
  const children = node.children || [];
  const [open, setOpen] = useState(depth < 6);
  const hasChildren = children.length > 0;

  return (
    <li>
      <div className="ast-row" onClick={() => hasChildren && setOpen(!open)}>
        <span className="twisty">{hasChildren ? (open ? "▾" : "▸") : "•"}</span>
        {node.role && <span className="role">{node.role}:</span>}
        <span className="ast-type">{node.type}</span>
        {node.label && <span className="ast-label mono">{node.label}</span>}
        <span className="muted pos">
          {node.line}:{node.column}
        </span>
      </div>
      {hasChildren && open && (
        <ul>
          {children.map((c, i) => (
            <Node key={i} node={c} depth={depth + 1} />
          ))}
        </ul>
      )}
    </li>
  );
}

export default function AstTree({ ast }) {
  if (!ast) return <p className="muted">No AST (the program did not parse).</p>;
  return (
    <div className="scroll">
      <ul className="tree root">
        <Node node={ast} depth={0} />
      </ul>
    </div>
  );
}