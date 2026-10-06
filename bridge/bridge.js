// CYD Home Web Bridge 1.0
// Keeps the Home Assistant/eWeLink credential on this computer or server,
// never on the CYD. Requires Node.js 18+ (built-in fetch, no npm install).
import http from "node:http";
import { readFileSync } from "node:fs";

let config;
try {
  config = JSON.parse(readFileSync(new URL("./config.json", import.meta.url)));
} catch {
  console.error("Missing bridge/config.json. Copy config.example.json to config.json and fill it in.");
  process.exit(1);
}

const port = Number(config.port || 8787);
const haUrl = String(config.homeAssistantUrl || "").replace(/\/+$/, "");
const haToken = String(config.homeAssistantToken || "");
const cydKey = String(config.cydKey || "");

if (!haUrl || !haToken || haToken.startsWith("PASTE_")) {
  console.error("Set homeAssistantUrl and homeAssistantToken in bridge/config.json.");
  process.exit(1);
}

function send(response, status, body) {
  response.writeHead(status, {
    "content-type": "application/json; charset=utf-8",
    "cache-control": "no-store",
  });
  response.end(JSON.stringify(body));
}

function authorized(request) {
  return !cydKey || request.headers["x-cyd-key"] === cydKey;
}

async function homeAssistant(path, method = "GET", body) {
  const response = await fetch(`${haUrl}/api${path}`, {
    method,
    headers: {
      authorization: `Bearer ${haToken}`,
      "content-type": "application/json",
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!response.ok) throw new Error(`Home Assistant returned ${response.status}`);
  return response.status === 204 ? null : response.json();
}

function toDevice(state) {
  const id = state.entity_id;
  const type = id.split(".", 1)[0];
  return {
    id,
    name: state.attributes?.friendly_name || id,
    type,
    state: state.state,
  };
}

function isSupported(entity) {
  return /^(switch|light|fan|cover)\./.test(entity.entity_id || "");
}

const server = http.createServer(async (request, response) => {
  try {
    if (!authorized(request)) return send(response, 401, { error: "Invalid CYD key" });
    const url = new URL(request.url, `http://${request.headers.host}`);

    if (request.method === "GET" && url.pathname === "/api/v1/status") {
      return send(response, 200, { ok: true, service: "CYD Home Web Bridge" });
    }

    if (request.method === "GET" && url.pathname === "/api/v1/devices") {
      const states = await homeAssistant("/states");
      return send(response, 200, { devices: states.filter(isSupported).slice(0, 20).map(toDevice) });
    }

    const match = url.pathname.match(/^\/api\/v1\/devices\/([^/]+)\/toggle$/);
    if (request.method === "POST" && match) {
      const entityId = decodeURIComponent(match[1]);
      if (!/^(switch|light|fan|cover)\.[a-zA-Z0-9_]+$/.test(entityId)) {
        return send(response, 400, { error: "Unsupported entity" });
      }
      const state = await homeAssistant(`/states/${entityId}`);
      const [type] = entityId.split(".");
      const isOn = state.state === "on" || state.state === "open";
      const service = type === "cover" ? (isOn ? "close_cover" : "open_cover") : (isOn ? "turn_off" : "turn_on");
      await homeAssistant(`/services/${type}/${service}`, "POST", { entity_id: entityId });
      const updated = await homeAssistant(`/states/${entityId}`);
      return send(response, 200, toDevice(updated));
    }

    return send(response, 404, { error: "Not found" });
  } catch (error) {
    console.error(error);
    return send(response, 502, { error: "Home Assistant is unavailable" });
  }
});

server.listen(port, "0.0.0.0", () => {
  console.log(`CYD Home Web Bridge listening on http://0.0.0.0:${port}`);
});
