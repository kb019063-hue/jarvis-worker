// ============================================================
// 🧠 JARVIS SYSTEM — PHASE 4.3 (ROOT CONTROL FOUNDATION)
// ============================================================

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname;

    return systemRouter(request, env, path);
  }
};

// ============================================================
// 🧠 SYSTEM STATE
// ============================================================

const SYSTEM_STATE = {
  version: "3.3.0",
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
// 👑 ROOT CONTROL LAYER
// ============================================================

const SYSTEM_CONTROL = {
  system_enabled: true,
  tools_enabled: true,
  memory_enabled: true,
  autonomous_enabled: false,
  mode: "normal"
};

// ============================================================
// ⚙️ SYSTEM ROUTER
// ============================================================

async function systemRouter(request, env, path) {

  // ❌ GLOBAL SYSTEM SHUTDOWN CHECK
  if (!SYSTEM_CONTROL.system_enabled && path !== "/root-command") {
    return jsonResponse({ error: "system_disabled" }, 503);
  }

  if (path === "/health") {
    return jsonResponse({
      status: "online",
      system: "JARVIS CORE",
      version: SYSTEM_STATE.version,
      phase: "4.3"
    });
  }

  if (path === "/system-state") {
    return jsonResponse({
      ...SYSTEM_STATE,
      control: SYSTEM_CONTROL
    });
  }

  if (path === "/root-command" && request.method === "POST") {
    return handleRootCommand(request, env);
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
// 🔐 ROOT AUTH
// ============================================================

async function rootPanelAuthGate(request, env) {
  const url = new URL(request.url);
  const key = url.searchParams.get("key");

  if (!key || key !== env.ROOT_KEY) {
    return jsonResponse({ auth: "denied" }, 401);
  }

  return jsonResponse({
    panel: "ROOT CONTROL ACTIVE",
    access: "granted"
  });
}

// ============================================================
// 👑 ROOT COMMAND HANDLER
// ============================================================

async function handleRootCommand(request, env) {
  try {
    const url = new URL(request.url);
    const key = url.searchParams.get("key");

    if (!key || key !== env.ROOT_KEY) {
      return jsonResponse({ error: "unauthorized" }, 401);
    }

    const body = await request.json();
    const command = body.command;
    const value = body.value;

    switch (command) {

      case "kill_switch":
        SYSTEM_CONTROL.system_enabled = false;
        return jsonResponse({ success: true, system_enabled: false });

      case "system_on":
        SYSTEM_CONTROL.system_enabled = true;
        return jsonResponse({ success: true, system_enabled: true });

      case "tools_off":
        SYSTEM_CONTROL.tools_enabled = false;
        return jsonResponse({ success: true, tools_enabled: false });

      case "tools_on":
        SYSTEM_CONTROL.tools_enabled = true;
        return jsonResponse({ success: true, tools_enabled: true });

      case "memory_off":
        SYSTEM_CONTROL.memory_enabled = false;
        return jsonResponse({ success: true, memory_enabled: false });

      case "memory_on":
        SYSTEM_CONTROL.memory_enabled = true;
        return jsonResponse({ success: true, memory_enabled: true });

      case "set_mode":
        SYSTEM_CONTROL.mode = value || "normal";
        return jsonResponse({ success: true, mode: SYSTEM_CONTROL.mode });

      case "autonomous_on":
        SYSTEM_CONTROL.autonomous_enabled = true;
        return jsonResponse({ success: true, autonomous_enabled: true });

      case "autonomous_off":
        SYSTEM_CONTROL.autonomous_enabled = false;
        return jsonResponse({ success: true, autonomous_enabled: false });

      default:
        return jsonResponse({ error: "unknown_command" }, 400);
    }

  } catch (err) {
    return jsonResponse({
      error: "root_command_failed",
      message: err.message
    }, 500);
  }
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
// 🧠 RESPONSE HELPER
// ============================================================

function jsonResponse(data, status = 200) {
  return new Response(JSON.stringify(data, null, 2), {
    status,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*"
    }
  });
}

// ============================================================
// 🧮 CALCULATOR ENGINE
// ============================================================

function evalBasic(expr) {
  const clean = String(expr).replace(/\s+/g, "");

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

    if (op === "+") values.push(a + b);
    if (op === "-") values.push(a - b);
    if (op === "*") values.push(a * b);
    if (op === "/") values.push(a / b);
  }

  for (let token of tokens) {
    if (!isNaN(token)) values.push(Number(token));
    else if (token === "(") ops.push(token);
    else if (token === ")") {
      while (ops.length && ops[ops.length - 1] !== "(") applyOp();
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

  ping: {
    meta: { name: "ping", domain: "core" },
    handler: async (_, context) => ({
      success: true,
      message: "PONG",
      memoryUsed: context.memory?.length || 0
    })
  },

  calculator: {
    meta: { name: "calculator", domain: "utility" },
    handler: async (input) => {
      try {
        const result = evalBasic(input);
        return { success: true, result };
      } catch (err) {
        return { success: false, error: err.message };
      }
    }
  },

  time: {
    meta: { name: "time", domain: "utility" },
    handler: async () => ({
      success: true,
      now: new Date().toISOString()
    })
  }
};

// ============================================================
// 🧠 MEMORY
// ============================================================

async function loadMemory(env, sessionId) {
  const raw = await env.JARVIS_KV.get(`chat:${sessionId}`);
  return raw ? JSON.parse(raw) : [];
}

async function saveMemory(env, sessionId, memory) {
  await env.JARVIS_KV.put(
    `chat:${sessionId}`,
    JSON.stringify(memory.slice(-20))
  );
}

// ============================================================
// ⚙️ INTERACTION HANDLER
// ============================================================

async function handleInteraction(request, env) {
  try {

    SYSTEM_STATE.telemetry.totalRequests++;
    SYSTEM_STATE.telemetry.lastActive = new Date().toISOString();

    const body = await request.json();

    const sessionId =
      request.headers.get("x-session-id") ||
      body.session_id ||
      "default";

    const toolEntry = TOOL_REGISTRY[body.action];

    if (!toolEntry) {
      return jsonResponse({ error: "tool_not_found" }, 404);
    }

    if (!SYSTEM_CONTROL.tools_enabled) {
      return jsonResponse({ error: "tools_disabled" }, 403);
    }

    let memory = [];
    if (SYSTEM_CONTROL.memory_enabled) {
      memory = await loadMemory(env, sessionId);
    }

    const result = await toolEntry.handler(body.input, { env, memory });

    if (SYSTEM_CONTROL.memory_enabled) {
      memory.push({ action: body.action, result });
      await saveMemory(env, sessionId, memory);
    }

    return jsonResponse({
      success: true,
      tool: toolEntry.meta.name,
      result
    });

  } catch (err) {

    SYSTEM_STATE.telemetry.totalErrors++;

    return jsonResponse({
      error: "interaction_failed",
      message: err.message
    }, 500);
  }
    }
