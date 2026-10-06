// CYD Home Web Bridge 2.0
// Serves eWeLink / Sonoff devices (via mwaqqp or Home Assistant) to CYD Home Hub.
// Requires Node.js 18+ (built-in fetch, zero dependencies).
import http from "node:http";
import { readFileSync, existsSync } from "node:fs";

let config = {};
if (existsSync(new URL("./config.json", import.meta.url))) {
  try {
    config = JSON.parse(readFileSync(new URL("./config.json", import.meta.url)));
  } catch {
    console.error("Warning: bridge/config.json invalid JSON. Using defaults.");
  }
}

const port = Number(config.port || 8787);
const mwaqqpUrl = String(config.mwaqqpUrl || "http://127.0.0.1:3000").replace(/\/+$/, "");
const haUrl = String(config.homeAssistantUrl || "").replace(/\/+$/, "");
const haToken = String(config.homeAssistantToken || "");
const cydKey = String(config.cydKey || "");

function send(response, status, body) {
  response.writeHead(status, {
    "content-type": "application/json; charset=utf-8",
    "cache-control": "no-store",
    "access-control-allow-origin": "*",
    "access-control-allow-headers": "Content-Type, X-CYD-Key",
  });
  response.end(JSON.stringify(body));
}

function authorized(request) {
  return !cydKey || request.headers["x-cyd-key"] === cydKey;
}

// Fetch devices from mwaqqp-main (eWeLink local server)
async function fetchMwaqqpDevices() {
  const res = await fetch(`${mwaqqpUrl}/api/ewelink/devices`, { signal: AbortSignal.timeout(4000) });
  if (!res.ok) throw new Error(`mwaqqp status ${res.status}`);
  const list = await res.json();
  return list.map(d => ({
    id: d.deviceid,
    name: d.name || d.deviceid,
    type: "switch",
    state: (d.params && d.params.switch === "on") ? "on" : "off",
    online: !!d.online
  }));
}

async function toggleMwaqqpDevice(deviceid) {
  const res = await fetch(`${mwaqqpUrl}/api/ewelink/action`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ deviceid, action: "turn" }),
    signal: AbortSignal.timeout(6000)
  });
  if (!res.ok) throw new Error(`mwaqqp action failed: ${res.status}`);
  const data = await res.json();
  return { id: deviceid, state: data.newState || "unknown" };
}

// Fetch devices from Home Assistant
async function homeAssistant(path, method = "GET", body) {
  if (!haUrl || !haToken || haToken.startsWith("PASTE_")) return null;
  const response = await fetch(`${haUrl}/api${path}`, {
    method,
    headers: {
      authorization: `Bearer ${haToken}`,
      "content-type": "application/json",
    },
    body: body ? JSON.stringify(body) : undefined,
    signal: AbortSignal.timeout(5000)
  });
  if (!response.ok) throw new Error(`Home Assistant returned ${response.status}`);
  return response.status === 204 ? null : response.json();
}

function haToDevice(state) {
  const id = state.entity_id;
  const type = id.split(".", 1)[0];
  return {
    id,
    name: state.attributes?.friendly_name || id,
    type,
    state: state.state,
    online: state.state !== "unavailable"
  };
}

function isHaSupported(entity) {
  return /^(switch|light|fan|cover)\./.test(entity.entity_id || "");
}

const server = http.createServer(async (request, response) => {
  try {
    if (request.method === "OPTIONS") {
      response.writeHead(204, {
        "access-control-allow-origin": "*",
        "access-control-allow-headers": "Content-Type, X-CYD-Key",
        "access-control-allow-methods": "GET, POST, OPTIONS"
      });
      return response.end();
    }

    if (!authorized(request)) return send(response, 401, { error: "Invalid CYD key" });
    const url = new URL(request.url, `http://${request.headers.host}`);

    if (request.method === "GET" && url.pathname === "/api/v1/status") {
      return send(response, 200, {
        ok: true,
        service: "CYD Home Web Bridge",
        mwaqqpUrl,
        hasHomeAssistant: !!(haUrl && haToken && !haToken.startsWith("PASTE_"))
      });
    }

    if (request.method === "GET" && url.pathname === "/api/v1/devices") {
      // Try mwaqqp (eWeLink) first
      try {
        const ewelinkDevices = await fetchMwaqqpDevices();
        if (ewelinkDevices && ewelinkDevices.length) {
          return send(response, 200, { devices: ewelinkDevices.slice(0, 20) });
        }
      } catch {
        // Fallback to Home Assistant if configured
      }

      if (haUrl && haToken && !haToken.startsWith("PASTE_")) {
        const states = await homeAssistant("/states");
        return send(response, 200, { devices: states.filter(isHaSupported).slice(0, 20).map(haToDevice) });
      }

      return send(response, 200, { devices: [] });
    }

    const match = url.pathname.match(/^\/api\/v1\/devices\/([^/]+)\/toggle$/);
    if (request.method === "POST" && match) {
      const entityId = decodeURIComponent(match[1]);

      // If it looks like an eWeLink device ID (alphanumeric, no dots)
      if (!entityId.includes(".")) {
        try {
          const result = await toggleMwaqqpDevice(entityId);
          return send(response, 200, result);
        } catch (err) {
          return send(response, 502, { error: err.message });
        }
      }

      // Otherwise Home Assistant
      if (haUrl && haToken && !haToken.startsWith("PASTE_")) {
        const state = await homeAssistant(`/states/${entityId}`);
        const [type] = entityId.split(".");
        const isOn = state.state === "on" || state.state === "open";
        const service = type === "cover" ? (isOn ? "close_cover" : "open_cover") : (isOn ? "turn_off" : "turn_on");
        await homeAssistant(`/services/${type}/${service}`, "POST", { entity_id: entityId });
        const updated = await homeAssistant(`/states/${entityId}`);
        return send(response, 200, haToDevice(updated));
      }

      return send(response, 400, { error: "Unsupported device" });
    }

    return send(response, 404, { error: "Not found" });
  } catch (error) {
    console.error(error);
    return send(response, 502, { error: error.message });
  }
});

server.listen(port, "0.0.0.0", () => {
  console.log(`\n==========================================`);
  console.log(`  CYD Home Web Bridge 2.0`);
  console.log(`  Listening on http://0.0.0.0:${port}`);
  console.log(`  eWeLink source : ${mwaqqpUrl}`);
  console.log(`  Home Assistant : ${haUrl || "None"}`);
  console.log(`==========================================\n`);
});
