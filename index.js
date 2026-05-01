// ============================================================  
// 🧠 JARVIS SYSTEM — CORE  
// ============================================================  

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname;

    return systemRouter(request, env, path);
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
      phase: 3,
      version: "v3.1.0",
      deployedAt: new Date().toISOString(),
      layers: 10,
      note: "Execution Core active"
    });
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
    message: "JARVIS SYSTEM RUNNING",
    phase: 3
  });
}

// ============================================================  
// 🔐 ROOT PANEL AUTH  
// ============================================================  

async function rootPanelAuthGate(request, env) {
  const url = new URL(request.url);
  const key = url.searchParams.get("key");

  if (!key || key !== env.ROOT_KEY) {
    return jsonResponse({
      auth: "denied",
      reason: "invalid_root_key"
    }, 401);
  }

  return jsonResponse({
    panel: "ROOT GOVERNANCE PANEL",
    auth: "root_key_verified",
    stage: "password_required_next",
    note: "Phase 1 auth stub only"
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
    },
    note: "Approximate location (Cloudflare metadata)"
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
// 🧮 SAFE CALCULATOR ENGINE  
// ============================================================  

function evalBasic(expr) {
  const clean = String(expr).replace(/\s+/g, "");

  if (!/^[0-9+\-*/().]+$/.test(clean)) {
    throw new Error("Invalid characters");
  }

  if (clean.includes("()")) {
    throw new Error("Empty brackets not allowed");
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

const STOPWATCH_STATE = new Map();

const TOOL_REGISTRY = {

  ping: async (input, context) => {
    const { memory } = context;

    return {
      success: true,
      tool: "ping",
      message: "PONG",
      memoryUsed: memory?.length || 0,
      lastAction: memory?.[memory.length - 1] || null,
      timestamp: Date.now()
    };
  },

  calculator: async (input) => {
    try {
      const expression = String(input || "");
      const sanitized = expression.replace(/[^0-9+\-*/().]/g, "");
      const result = evalBasic(sanitized);

      return {
        success: true,
        tool: "calculator",
        expression: sanitized,
        result,
        timestamp: Date.now()
      };
    } catch (err) {
      return {
        success: false,
        tool: "calculator",
        error: err.message
      };
    }
  },

  stopwatch: async (input, context) => {
    const { request } = context;

    const sessionId =
      request.headers.get("x-session-id") || "default";

    const action = String(input || "").toLowerCase();

    let state = STOPWATCH_STATE.get(sessionId);

    if (!state) {
      state = { running: false, startTime: 0, elapsed: 0 };
      STOPWATCH_STATE.set(sessionId, state);
    }

    if (action === "start") {
      state.running = true;
      state.startTime = Date.now();
      return { success: true, status: "started" };
    }

    if (action === "stop") {
      if (!state.running) {
        return { success: false, error: "not_running" };
      }

      state.elapsed += Date.now() - state.startTime;
      state.running = false;

      return {
        success: true,
        status: "stopped",
        elapsed_ms: state.elapsed
      };
    }

    if (action === "reset") {
      state.running = false;
      state.startTime = 0;
      state.elapsed = 0;
      return { success: true, status: "reset" };
    }

    if (action === "status") {
      let current = state.elapsed;

      if (state.running) {
        current += Date.now() - state.startTime;
      }

      return {
        success: true,
        status: "status",
        running: state.running,
        elapsed_ms: current
      };
    }

    return {
      success: false,
      tool: "stopwatch",
      error: "invalid_action",
      message: "use start | stop | reset | status"
    };
  },

  memory_set: async (input, context) => {
    const { env } = context;
    const { key, value } = input || {};

    if (!key || value === undefined) {
      return { success: false, error: "missing_key_or_value" };
    }

    await env.JARVIS_KV.put(key, JSON.stringify(value));

    return { success: true, tool: "memory_set", key };
  },

  memory_get: async (input, context) => {
    const { env } = context;
    const { key } = input || {};

    if (!key) {
      return { success: false, error: "missing_key" };
    }

    const value = await env.JARVIS_KV.get(key);

    return {
      success: true,
      tool: "memory_get",
      key,
      value: value ? JSON.parse(value) : null
    };
  },

  time: async () => {
    const now = new Date();

    return {
      success: true,
      tool: "time",
      data: {
        iso: now.toISOString(),
        local: now.toString()
      }
    };
  },

  calendar: async () => {
    const now = new Date();

    return {
      success: true,
      tool: "calendar",
      data: {
        year: now.getFullYear(),
        month: now.getMonth() + 1,
        day: now.getDate(),
        weekday: now.toLocaleString("en-US", { weekday: "long" })
      }
    };
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
// ⚙️ INTERACTION  
// ============================================================  

async function handleInteraction(request, env) {
  try {
    const body = await request.json();

    const sessionId =
      request.headers.get("x-session-id") ||
      body.session_id ||
      "default";

    const action = body.action;
    const input = body.input || null;

    if (!action) {
      return jsonResponse(
        { error: "missing_action", message: "Action is required" },
        400
      );
    }

    const tool = TOOL_REGISTRY[action];

    if (!tool) {
      return jsonResponse(
        { status: "error", error: "tool_not_found", action },
        404
      );
    }

    const memory = await loadMemory(env, sessionId);

    const result = await tool(input, { request, env, memory });

    memory.push({ action, input, result, time: Date.now() });

    await saveMemory(env, sessionId, memory);

    return jsonResponse({
      status: "success",
      sessionId,
      action,
      result,
      memorySize: memory.length
    });

  } catch (err) {
    return jsonResponse({
      error: "interaction_failed",
      message: err.message
    }, 500);
  }
}
