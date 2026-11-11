// All communication with the C server lives here.
// In dev the Vite proxy forwards /api to http://127.0.0.1:8080.
// To talk to a server elsewhere, create frontend/.env with:  VITE_API_URL=http://host:port
const BASE = (import.meta.env && import.meta.env.VITE_API_URL) || "";

export async function compileSource(source) {
  let response;
  try {
    response = await fetch(`${BASE}/api/compile`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source }),
    });
  } catch {
    throw new Error(
      "Cannot reach the compiler server. Start it with ./compiler_server (port 8080)."
    );
  }

  let data;
  try {
    data = await response.json();
  } catch {
    throw new Error(`The server sent an invalid response (HTTP ${response.status}).`);
  }
  if (data.stage === "request") throw new Error(data.error || "Bad request");
  return data;
}

export async function checkHealth() {
  try {
    const response = await fetch(`${BASE}/api/health`);
    return response.ok;
  } catch {
    return false;
  }
}