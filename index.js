// ============================================================
// 🧠 JARVIS SYSTEM — PHASE 4 FOUNDATION (CLEAN BUILD)
// ============================================================

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname;

    return systemRouter(request, env, path);
  }
};

// ============================================================
// 🧠 SYSTEM STATE (PHASE 4)
// ============================================================

const SYSTEM_STATE = {
  version: "3.2.0-Alpha",
  status: "OPERATIONAL",
  telemetry: {
    totalRequests: 0,
    totalErrors: 0,
    lastActive: null,
  },
  capabilities: {
    edge_logic: true,
    heavy_muscle: false,
    vision_active: false,
    mutation_ready: false
  },
  limits: {
    kv_writes_today: 0,
    compute_units_avg: 0
  }
};

// ============================================================
// ⚙️ SYSTEM ROUTER
// ============================================================

async function systemRouter(request, env, path) {

  if (path === "/health") {
    return jsonResponse({
      status: "online",
      system: "JARVIS CORE",
      version: SYSTEM_STATE.version,
      phase: 4
    });
  }

  if (path === "/system-state") {
    return jsonResponse(SYSTEM_STATE);
  }

  if (path === "/root-panel") {
    return rootPanelAuthGate(request, env);
  }

  if (path === "/location") {
    return locationTool(request);
  }

  if (path === "/api/v1/interact" && request.method === "POST") {
    return handleInteraction(request, env);
  }

  return jsonResponse({
    status: "active",
    message: "JARVIS SYSTEM RUNNING"
  });
}

// ============================================================
// 🔐 ROOT PANEL AUTH
// ============================================================

async function rootPanelAuthGate(request, env) {
  const url = new URL(request.url);
  const key = url.searchParams.get("key");

  if (!key || key !== env.ROOT_KEY) {
    return jsonResponse({ auth: "denied" }, 401);
  }

  return jsonResponse({
    panel: "ROOT GOVERNANCE PANEL",
    auth: "verified"
  });
}

// ============================================================
// 📍 LOCATION TOOL
// ============================================================

function locationTool(request) {
  const cf = request.cf || {};

  return jsonResponse({
    location: {
      country: cf.country || "unknown",
      city: cf.city || "unknown",
      region: cf.region || "unknown",
      timezone: cf.timezone || "unknown"
    }
  });
}

// ============================================================
// 🧠 SAFE RESPONSE HELPER
// ============================================================

function jsonResponse(data, status = 200) {
  let safe;

  try {
    safe = JSON.stringify(data, null, 2);
  } catch {
    safe = JSON.stringify({ error: "serialization_failed" });
  }

  return new Response(safe, {
    status,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*"
    }
  });
}

// ============================================================
// 🧮 SAFE CALCULATOR ENGINE (HARDENED)
// ============================================================

function evalBasic(expr) {
  const clean = String(expr || "").replace(/\s+/g, "");

  if (!clean) throw new Error("Empty expression");

  if (!/^[0-9+\-*/().]+$/.test(clean)) {
    throw new Error("Invalid characters");
  }

  const tokens = clean.match(/\d+|[+\-*/()]/g);
  if (!tokens) throw new Error("Invalid expression");

  const values = [];
  const ops = [];

  const precedence = { "+": 1, "-": 1, "*": 2, "/": 2 };

  function applyOp() {
    const b = values.pop();
    const a = values.pop();
    const op = ops.pop();

    if (a === undefined || b === undefined || !op) return;

    if (op === "+") values.push(a + b);
    if (op === "-") values.push(a - b);
    if (op === "*") values.push(a * b);
    if (op === "/") values.push(a / b);
  }

  for (let token of tokens) {
    if (!isNaN(token)) {
      values.push(Number(token));
    } else if (token === "(") {
      ops.push(token);
    } else if (token === ")") {
      while (ops.length && ops[ops.length - 1] !== "(") {
        applyOp();
      }
      ops.pop();
    } else {
      while (
        ops.length &&
        ops[ops.length - 1] !== "(" &&
        precedence[ops[ops.length - 1]] >= precedence[token]
      ) {
        applyOp();
      }
      ops.push(token);
    }
  }

  while (ops.length) applyOp();

  return values[0];
}

// ============================================================
// 🧩 TOOL REGISTRY
// ============================================================

const TOOL_REGISTRY = {

  ping: async (_, context) => ({
    success: true,
    message: "PONG",
    memoryUsed: context.memory?.length || 0
  }),

  calculator: async (input) => {
    try {
      const result = evalBasic(input);
      return { success: true, result };
    } catch (err) {
      return { success: false, error: err.message };
    }
  },

  time: async () => ({
    success: true,
    now: new Date().toISOString()
  })
};

// ============================================================
// 🧠 MEMORY
// ============================================================

async function loadMemory(env, sessionId) {
  try {
    const raw = await env.JARVIS_KV.get(`chat:${sessionId}`);
    return raw ? JSON.parse(raw) : [];
  } catch {
    return [];
  }
}

async function saveMemory(env, sessionId, memory) {
  await env.JARVIS_KV.put(
    `chat:${sessionId}`,
    JSON.stringify(memory.slice(-20))
  );
}

// ============================================================
// ⚙️ INTERACTION HANDLER (WITH TELEMETRY)
// ============================================================

async function handleInteraction(request, env) {
  try {

    // ⚡ TELEMETRY
    SYSTEM_STATE.telemetry.totalRequests++;
    SYSTEM_STATE.telemetry.lastActive = new Date().toISOString();

    console.log(
      `[JARVIS] #${SYSTEM_STATE.telemetry.totalRequests} ACTIVE`
    );

    const body = await request.json();

    const sessionId =
      request.headers.get("x-session-id") ||
      body.session_id ||
      "default";

    const tool = TOOL_REGISTRY[body.action];

    if (!tool) {
      return jsonResponse({ error: "tool_not_found" }, 404);
    }

    const memory = await loadMemory(env, sessionId);
    const result = await tool(body.input, { env, memory });

    memory.push({
      action: body.action,
      result
    });

    await saveMemory(env, sessionId, memory);

    return jsonResponse({ success: true, result });

  } catch (err) {

    SYSTEM_STATE.telemetry.totalErrors++;

    return jsonResponse({
      error: "interaction_failed",
      message: err.message
    }, 500);
  }
}
