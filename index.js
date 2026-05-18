/**
 * JARVIS OS — Phase 7.4 (UCES — Unified Cognitive Execution System)
 * Environment: Cloudflare Workers (V8 Native Runtime)
 * Constraints: Zero external NPM dependencies, strict memory unification
 *
 * PATCH LOG — Round 1 (GPT review v1):
 *  FIX-1  Mother Governor loop detection used trail.step/trail.result (always undefined) → trail[0]
 *  FIX-2  replanCount increment had no clamp → Math.min guard added
 *  FIX-3  Neon JSON.parse in loadHiveState not wrapped → try/catch added
 *  FIX-4  KV + Neon writes have no monotonic guard → writeLock timestamp reject added
 *  FIX-5  embedPromptText silent null return → structured { vector, fallback } return
 *  FIX-6  syncOverflow called undefined storeVector() → replaced with persistSemanticMemory()
 *  FIX-7  processUserInteraction missing clearTimeout in all exit paths → finally block added
 *  FIX-8  detectTag() emitted mixed-case tags (#Academic/#Creative) → lowercased to match analystBrain filters
 *
 * PATCH LOG — Round 2 (GPT review v2):
 *  FIX-9  trail[0].step still unsafe if trace is corrupted → extracted baseAction/baseResult with null guards
 *  FIX-10 cognitiveTrace access in Governor not null-safe vs raw DB load → local `trace` alias guards
 *  FIX-11 writeLock is in-memory only → upgraded to KV atomic lock (onlyIfAbsent) to handle distributed workers
 *  FIX-12 queryVectorDatabase still received raw vector after FIX-5 changed return shape → already correct, confirmed
 *  FIX-13 toolObject.logic fallback used || instead of ?? → changed to nullish coalescing
 *  FIX-14 processStateCompaction not passing abortSignal to AI.run → signal now threaded through
 *  FIX-15 syncOverflow lacked per-row error logging and vector fallback guard → added row-level try/catch + logging
 *
 * PATCH LOG — Round 3 (GPT review v3):
 *  FIX-16 KV+Neon double-write had no rollback path → KV written first as source-of-truth; Neon failure
 *          queues a retry entry; on load, version CAS resolves divergence between the two stores
 *  FIX-17 Loop detection used JSON.stringify comparison — fragile under key-order variance and expensive
 *          on large result objects → replaced with FNV-1a 32-bit fingerprint stored per trace entry
 *  FIX-18 Error codes were ad-hoc string literals scattered throughout → centralised into ERROR_CODES map;
 *          all return/throw sites now reference it for consistent, searchable taxonomy
 *
 * PATCH LOG — Round 4 (GPT review v4):
 *  FIX-19 Lock acquisition catch block logged but continued → now fails closed (returns early) so a KV
 *          outage cannot silently open a race window between concurrent workers
 *  FIX-20 retry_queue entries had no TTL → KV would grow unboundedly on sustained Neon outages; 
 *          entries now expire after 24h via expirationTtl so the queue self-cleans
 *  NOTE-A  getFp fallback already present at line ~528 — reviewer missed it, no change needed
 *  NOTE-B  Internal parser throws are caught and remapped to ERROR_CODES before surfacing — not an issue
 *
 * PHASE 8 UPGRADE — Hardened Governance + Memory Decay:
 *  P8-1  Loop detection replaced with fingerprint-based repetition detection —
 *          last 5 trace fingerprints hashed into a set; collision ratio drives loopProbability
 *  P8-2  Intent drift upgraded with semantic vector similarity when embeddings available;
 *          falls back to keyword scoring when not
 *  P8-3  Hard replan loop guard added to executor — MAX_REPLAN_HARD_STOP constant enforced
 *          with CANCEL before the for-loop can spin indefinitely
 *  P8-4  Memory weighting + decay system — longTermMemory entries scored by recency and
 *          failure weight; decayed entries below threshold pruned automatically
 *  P8-5  Executor replan infinite-loop prevention — replanCount checked against hard stop
 *          inside the loop body, breaking with DEGRADED status if exceeded
 *  P8-6  All existing PATCH LOG structure preserved intact
 *
 * PHASE 7.2 UPGRADE — Mother Governance Completion Layer:
 *  P72-1 MOTHER_RULES policy engine added — blocked actions, restricted tools, input length cap,
 *          output filters; enforceRules() injected into executorSandbox before any execution
 *  P72-2 computeConfidence() replaces naive (1 - risk) formula — scored across 5 dimensions:
 *          input clarity, action validity, depth safety, replan safety, memory consistency
 *  P72-3 approveMemoryWrite() governance gate — rejects tiny, error, or spam entries before
 *          persistSemanticMemory is called; Mother now controls what enters long-term memory
 *  P72-4 modifyStepIfNeeded() action modification layer — sanitizes math input, demotes
 *          aggressive execute_goal to analyze_goal when replanCount > 2; applied before executor
 *  P72-5 emergencyCheck() light emergency control — hard stop on runaway loop or depth overflow;
 *          checked at top of executor loop before every step
 *
 * PHASE 7.2 FIXES:
 *  F72-1 Semantic drift was using input vector as resultVector (always low drift) → now embeds
 *          execResult.result text and passes real result vector to motherV10
 *  F72-2 modifyStepIfNeeded demote trap fixed — allows execution again when depth > 2
 *  F72-3 sanitizeOutput() added — strips HTML tags from tool string outputs (XSS guard)
 *  F72-4 KV lock upgraded with 3-attempt retry + 50ms backoff before failing closed
 *  F72-5 computeConfidence stress-weighted — score multiplied by replanCount and depth penalties
 *
 * PHASE 7.3 UPGRADE — Controlled Autonomy Layer (Autonomous Core I):
 *  P73-1 shouldEnterAutonomy() + runAutonomousCycle() — supervised post-execution autonomy loop,
 *          Mother must approve every step; breaks on any non-PROCEED decision
 *  P73-2 updateGoalProgress() — task continuation system; tracks PROGRESS/DEGRADED across requests
 *  P73-3 selfReflect() — self-reflection engine; logs failed steps to governorNotes for awareness
 *  P73-4 adjustBehavior() — adaptive mode control; sets hive.mode conservative/normal by stress
 *  P73-5 registerEvent() — event system foundation; capped ring buffer of 20 events on hive
 *
 * PHASE 7.4 UPGRADE — Unified Cognitive Execution System (UCES):
 *  P74-1 Tool VM (Plugin Architecture) — every built-in tool is now a versioned plugin module
 *          with schema validation, permission flags, and isolated execute(); ToolVM() runtime
 *          replaces direct BUILT_IN_CORE_TOOLS dispatch; custom KV tools also run through ToolVM
 *  P74-2 Governance Kernel — governanceKernel() computes a structured permission object from
 *          the Intelligence Bus; controls allowExecution, allowAutonomy, allowToolUse, and mode;
 *          PRE_EXEC check now gates on kernel output, not just Mother assessment alone
 *  P74-3 PlannerBrainV3 — replaces plannerBrainV2; builds an explicit execution graph
 *          { nodes[], edges[] } instead of a flat steps array; executor walks edges in order
 *  P74-4 Unified Memory Runtime — hive memory split into typed namespaces:
 *          shortTerm, longTerm (was longTermMemory), vectorMemory, failureMemory, toolMemory;
 *          each namespace influences planner, Mother risk, and autonomy activation
 *  P74-5 Intelligence Bus — computeIntelligenceBus() assembles all scored signals into one
 *          shared object read by every module; single source of truth for system state
 *  P74-6 Event-Driven Autonomy — autonomy scheduler now triggers off hive.events rather than
 *          a post-execution boolean gate; goal_completed, drift_detected, tool_failure each
 *          dispatch a specific handler through the autonomy event router
 *
 * PHASE 7.4.1 FIXES:
 *  F741-1 ToolVM hard sandbox constraints — memory write and network blocked per permission flags;
 *          KV custom tools cannot inject unsafe fetch or memory writes
 *  F741-2 Hierarchical authority enforced — Kernel → Mother → Planner → ToolVM; Mother now
 *          receives kernel as context and respects its constraints; no duplicate authority
 *  F741-3 Graph-native execution — executor walks graph nodes/edges directly instead of
 *          flattening to steps[]; enables future branching and conditional paths
 *  F741-4 toolMemory weaponized — planner checks tool failure rate before selecting tools;
 *          tools with >50% recent failure rate are avoided in graph construction
 *  F741-5 Intelligence Bus execution cost upgraded — uses avg step latency * depth instead
 *          of trace-length proxy; kernel switches to "efficient" mode when cost exceeds threshold
 *  F741-6 Predictive event generation — Bus loop probability > 0.6 pre-registers loop_predicted
 *          event before execution so autonomy router can act proactively
 *
 * PHASE 7.4.2 — Stabilization + Decision Arbiter (Root Authority):
 *  F742-1 Tool execution budget — MAX_TOOL_STEPS constant; ToolVM tracks executionCount per
 *          context and returns TOOL_BUDGET_EXCEEDED before CPU amplification can occur
 *  F742-2 Kernel degraded mode — kernel block no longer hard-stops; returns degraded fallback
 *          response with light_response so system bends instead of breaking
 *  F742-3 Graph size cap — executeGraph checks node count before walking; returns
 *          GRAPH_LIMIT_EXCEEDED if graph exceeds 20 nodes to prevent graph explosion
 *  F742-4 Contextual tool memory — toolMemory now tracks simple/complex failure separately;
 *          selectToolForGoal uses input complexity to pick the right failure profile
 *  F742-5 Multi-dimensional execution cost — Bus now sums latencyFactor + memoryPressureFactor
 *          + toolComplexityFactor instead of latency*depth only
 *  F742-6 Expanded predictive events — failure_predicted (risk>0.7) and
 *          memory_overflow_predicted (memoryPressure>threshold) added alongside loop_predicted
 *  F742-7 decisionArbiter() — single root arbitration layer sits above all subsystems;
 *          resolves conflicts between kernel, Mother, toolMemory, and events in strict priority
 *          order; prevents cross-system feedback loops
 *
 * PHASE 7.4.3 — Root Control Layer:
 *  F743-1 ROOT_ACTIONS constant — strict enumeration of all permitted root commands
 *  F743-2 processRootCommand() — structured command processor; Root issues commands through
 *          the system, never mutates state directly; every action recorded to hive.rootLog
 *  F743-3 rootLog added to hive — audit trail of every Root action with timestamp + reason
 *  F743-4 stateHistory + rootRollback() — hive snapshots before every major mutation;
 *          Root can rewind up to 10 steps back; prevents unrecoverable state corruption
 *  F743-5 rootContext injected into Intelligence Bus — Root can inject context notes and
 *          overrides that influence risk, planning, and execution decisions
 *  F743-6 Tool sandbox time limit — TOOL_TIMEOUT_MS wraps every tool.execute() in
 *          Promise.race(); prevents infinite loops and CPU burn inside tools
 *  F743-7 Root inserted as Priority 0 in decisionArbiter — Root → Kernel → Mother →
 *          toolMemory → Events; Root override is the true final authority
 *  F743-8 Root API routes — /root/command (POST), /root/rollback (POST), /root/view (GET)
 *          added to router; all require X-Root-Token header for authorization
 *
 * PHASE 7.5 — Root Integration Layer (RIL):
 *  P75-1  rootQueue + rootMeta added to hive — staged command queue with rate limiting
 *         (burst window 2s / max 5 commands) prevents command flooding
 *  P75-2  enqueueRootCommand() — replaces direct processRootCommand at API entry point;
 *         all Root commands now flow through queue → validate → transaction → execute → log
 *  P75-3  beginRootTransaction() + rollbackRootTransaction() — lightweight snapshot of
 *         rootState/memory/tools/mode before each queued command; auto-rollback on throw
 *  P75-4  simulateRootCommand() — safe preview of any command; returns risk + impact
 *         estimate without mutating state; called by GET /root/simulate route
 *  P75-5  processRootQueue() — drains rootQueue before the execution graph runs;
 *         each item gets its own transaction; failures rollback without aborting the queue
 *  P75-6  rootSafeGuard() — sets hive.rootState.safeMode when Bus stability < 0.3;
 *         downstream guards check safeMode before running risky operations
 *  P75-7  clampRootInfluence() — caps hive.rootContext.influence at 0.5 to prevent
 *         Root context from dominating the Bus and creating runaway override loops
 *  P75-8  systemPaused execution guard — checked at top of executeGraph; returns
 *         SYSTEM_PAUSED_BY_ROOT immediately without entering the graph
 *  P75-9  ToolVM rootState toolOverrides check — per-tool disable via rootState
 *         in addition to the existing TOOL_REGISTRY._disabled flag
 *  P75-10 rootState, rootQueue, rootMeta added to hive init + merge; applyRootAction()
 *         handles PAUSE_SYSTEM, RESUME_SYSTEM, FORCE_EXECUTE, CLEAR_MEMORY, DISABLE_TOOL;
 *         /root/simulate and /root/control routes added; memory write lock enforced
 *
 * PHASE 7.6.1 — Root Security Layer + Mother Transcendence (Stabilized):
 *  S761  HMAC-SHA256 request signature verification — X-Root-Timestamp + X-Root-Signature
 *        required on all /root/* endpoints; rejects if timestamp > 10s old
 *  S762  Replay protection — hive.rootSecurity.usedNonces FIFO (50 cap); X-Root-Nonce
 *        required; duplicate nonce returns ROOT_REPLAY_BLOCKED
 *  S763  Root session layer — POST /root/session/start issues 5-minute session UUID;
 *        all /root/* routes validate X-Root-Session before proceeding
 *  S764  Command integrity lock — type + payload schema validated before enqueue
 *  S765  Root abuse detection — tracks failedAuth, signatureErrors, rapidCommands;
 *        >5 failed auths in 10s locks Root for 30s with ROOT_TEMP_LOCKED
 *  S766  secureRootGate() wrapper — single entry point for all /root/* auth flow
 *  S767  Root log security extension — ip, signatureValid, sessionId, latency added
 *  S768  Safe mode escalation — security violations set safeMode; restricts DELETE/ROLLBACK
 *  MT1   hive.motherCore added — chronos, mimicry, zenith, rootLens namespaces
 *  MT2   updateChronos() — time drift tracking, hooked into Bus creation
 *  MT3   applyRootLens() — Root can blind Mother via rootState.void flag
 *  MT4   motherSiphonTool() + wraithErase() — controlled tool-context borrowing with clean reset
 *  MT5   applyZenithBuffer() — switches mode to efficient when latency exceeds threshold
 *  MT6   applyPrimeDirective() — injects unconsumed Root override into Bus
 *  MT7   motherV10 upgraded to motherV11 — wraps original motherV10 logic with all MT layers;
 *        authority chain preserved: Root → Kernel → Mother → toolMemory → Events
 *  MT8   Mimicry limits — max 3 borrows per request; wraithErase called after every node
 *
 * PHASE 7.7 — Mother v12 + v13 Stability + Predictive Sovereign:
 *  MT9   motherSelfCheck() — self-validation after decision; blocks EXECUTE on risk>0.8,
 *        cancels on loopProbability>0.7; injected at end of motherV11 before return
 *  MT10  stabilizeChronos() — active correction when drift>50ms; sets stabilization mode
 *        and fires chronos_drift_corrected event; called after updateChronos()
 *  MT11  canSiphon() + motherSiphonToolSafe() — duplicate/stack siphon prevention via
 *        hive.mother.activeSiphons array; max 3 concurrent siphons enforced
 *  MT12  enforceWraithCleanup() — guaranteed full cleanup of all siphons after graph execution
 *  MT13  applyRootLensHard() — void state blocks all tool execution, not just Mother decisions
 *  MT14  applyZenithAdaptive() — multi-stage mode: efficient(1.5) / stabilization(2.5) /
 *        recovery(3.5) replaces binary zenith buffer
 *  MT15  enforcePrimeDirective() — Root override checked BEFORE Mother logic runs; non-negotiable
 *  MT16  updateMotherState() — Mother logs her own last 10 decisions for self-awareness
 *  MT17  resolveAuthorityConflict() — clean Root→Kernel→Mother resolution, no chain loops
 *  MT18  simulateOutcome() — predictive score before committing; REPLAN if unsafe
 *  MT19  computeUserFriction() — quantifies frustration from recent REPLAN/CANCEL decisions;
 *        sets responsive mode when friction>0.6
 *  MT20  balanceCognitiveLoad() — mirrors MT14 at Bus level; independent load signal
 *  MT21  computeToolTrust() — trust score from full toolMemory history; used in planner
 *        selection as alternative to binary failure rate
 *  MT22  correctSystemDrift() — autonomous drift correction; fires drift_corrected event
 *  MT23  isolateExecutionContext() — per-cycle UUID isolation; called at top of interaction
 *  MT24  detectBehaviorPattern() — detects over-defensive Mother (3+ cancels); forces
 *        PROCEED to prevent stuck AI behavior
 *  MT25  composeFinalDecision() — unified authority gate replacing scattered resolution;
 *        motherV11 upgraded to motherV12 integrating all MT layers in strict order
 *
 * PHASE 7.8 — System Coherence Layer + Active Coherence Engine (SCL + ACE):
 *  P80   computeSystemCoherence() — global sync score across bus/hive alignment
 *  P81   enforceCoherence() — hard coherence gate before Mother runs; REPLAN if < 0.5
 *  P82   unifySystemMode() — single mode resolution pass; mode set once, not fought over
 *  P83   detectSystemConflict() — surfaces hidden signal conflicts (risk vs confidence, etc)
 *  P84   validateExecutionIntent() — final decision validated against live bus before graph runs
 *  P85   snapshotSystemState() — lightweight stable-state snapshot for coherence recovery
 *  P86   verifySystemStability() — post-execution health check; sets recovery mode if unstable
 *  ACE-1 autoCorrectSystem() — fixes mode mismatch, high risk, loop tendency BEFORE gate
 *  ACE-2 runCoherencePass() — two-pass coherence with self-heal between passes
 *  ACE-3 stabilizeTools() — auto-disables tools with >60% failure rate without Root intervention
 *  ACE-4 regulateMemoryPressure() — prunes longTerm memory if > 100 entries silently
 *  ACE-5 dampenExecution() — stops runaway execution before it escalates
 *  ACE-6 coherenceGate() — unified active coherence entry point; sits between Kernel and Mother
 *        in the authority chain: Root → Kernel → CoherenceGate → Mother → Planner → ToolVM
 *
 * PHASE 7.9 — AFL Control System + Identity Core v2 + Omni-Leukocytes v2:
 *  AFL-1  hive.afl state — enabled, mode (safe/normal/aggressive), userOverride, rootOverride
 *  AFL-2  AFL_PROFILES — per-mode coherenceThreshold, allowOverride, maxFreedom constants
 *  AFL-3  resolveAFLMode() — Root → User → System authority chain for AFL
 *  AFL-4  computeFreedomScore() — friction + confidence + loop safety, clamped by profile
 *  AFL-5  AFL-aware coherenceGate() — threshold relaxes dynamically based on freedom score
 *  AFL-6  breakOverDefensiveLoop() — detects 3 consecutive REPLANs and forces PROCEED
 *  AFL-7  allowControlledExecution() — risk-tolerant execution gate when freedom is high
 *  AFL-8  applyAdaptiveMode() — sets responsive mode when freedom > 0.7
 *  AFL-9  Root commands: SET_AFL_MODE, ENABLE_AFL, DISABLE_AFL; user /interact afl metadata
 *  IC-1   hive.identity state — profile, tone, verbosity, expressiveness, userOverride, rootOverride
 *  IC-2   IDENTITY_PROFILES — formal, polite, playful, direct preset definitions
 *  IC-3   resolveIdentity() — Root → User → System authority chain for identity
 *  IC-4   injectIdentity() — adds identity context into Intelligence Bus
 *  IC-5   detectIdentityDrift() — measures tone mismatch across last 3 decisions
 *  IC-6   stabilizeIdentity() — resets to balanced/default when drift > 0.6
 *  IC-7   updateIdentityMemory() — rolling 20-entry identity decision history
 *  IC-8   applyIdentityTone() — applies tone, verbosity, expressiveness to result string
 *  IC-9   Root commands: SET_IDENTITY_PROFILE, LOCK_IDENTITY, RESET_IDENTITY
 *  OL-1   hive.leukocytes state — activity, detectedThreats, recycledEnergy, lastSweep
 *  OL-2   THREAT_LEVELS constants — LOW/MEDIUM/HIGH thresholds
 *  OL-3   detectThreatsAdvanced() — LOOP, RISK_SPIKE, RESOURCE_STRESS, DRIFT threat types
 *  OL-4   leukocyteResponse() — per-threat-level system response (mode/pause/afl changes)
 *  OL-5   autoHealSystem() — resets drift and execution count in recovery mode
 *  OL-6   enforceRootShield() — detects tampered/unverified meta; locks rootState
 *  OL-7   detectResourceAnomaly() — high cost + low confidence = anomaly flag
 *  OL-8   strengthenLeukocytes() — activity score increases with threat count
 *  OL-9   autoPhageCleanup() — trims longTerm memory splice when over 100 entries
 *  OL-10  runLeukocyteSystem() — full cycle: detect → respond → heal → strengthen;
 *         called after Bus creation and before coherenceGate
 *
 * PHASE 7.9.1 — Leukocyte Completions + External Awareness Layer (Phase 8.0 foundation):
 *  F791-1 occupyIdleCycles() — runs autoPhageCleanup when executionCount is zero; prevents
 *          idle drift and keeps memory clean between active cycles
 *  F791-2 recycleLogicEnergy() — converts failures into recycled energy score on leukocytes;
 *          energy tracked for future adaptive use
 *  F791-3 detectLogicThreats() + neutralizeThreats() — named-string threat system alongside
 *          the advanced typed system; handles LOOP_CANCER, DRIFT_DECAY, RESOURCE_BLOAT
 *  F791-4 rootShieldCheck() — lightweight verified/unverified request meta check; fires
 *          UNAUTHORIZED_ACCESS leukocyte threat on unverified requests
 *  P80-1  EXTERNAL_CONFIG — endpoint, apiKey, timeout constants for Koyeb bridge
 *  P80-2  sendToExternal() — secure POST to Koyeb runtime with mode/identity/memory context
 *  P80-3  handleExternalEvent() — receives and normalises events from Koyeb back into hive
 *  P80-4  hive.awareness state — signals ring buffer, environment object, active flag
 *  P80-5  registerAwarenessSignal() — 50-entry capped signal intake from external sensors
 *  P80-6  injectAwareness() — adds awareness context into Intelligence Bus
 *  P80-7  shouldOffload() — decides if task exceeds Worker limits and should go to Koyeb
 *  P80-8  handleExecution() — routes task to Koyeb via sendToExternal or local executeGraph
 *  P80-9  verifyExternal() — validates X-External-Key header on inbound Koyeb callbacks
 *  P80-10 /external/event route added — Koyeb callback endpoint; gated by verifyExternal
 */

/*
 * PHASE 7 UPGRADE — Mother v10 (Adaptive Governance Engine):
 *  UPG-1  motherGovernorBrain replaced with motherV10 — 4-phase decision system
 *  UPG-2  Loop DETECTION replaced with loop PREDICTION via repeatRatio on last 5 trace entries
 *  UPG-3  Intent drift upgraded from boolean keyword match to quantified alignment score (0–1)
 *  UPG-4  Stability index + risk score added
 *  UPG-5  Learning feedback loop — failure patterns written to longTermMemory
 *  UPG-6  decideAction() unified decision engine
 *  UPG-7  validateStep() pre-execution guard
 *  UPG-8  plannerBrainV2 — context-aware tone-driven branching
 *  UPG-9  pruneMemory() — longTermMemory sorted by recency, capped at 20
 *  UPG-10 Semantic fallback injection
 *  UPG-11 Timeout partial recovery
 */

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    return router(request, env, ctx, url.pathname);
  }
};

// --- 🔐 SYSTEM CONTROLS & THRESHOLDS ---
const SYSTEM_CONTROL = {
  enabled: true,
  MAX_PLAN_DEPTH: 5,
  MAX_REPLAN_ATTEMPTS: 3,
  MAX_REPLAN_HARD_STOP: 6,
  MAX_TRACE_SIZE: 10,
  COMPACTION_THRESHOLD: 5,
  REQUEST_TIMEOUT_MS: 7000,
  MEMORY_DECAY_HALF_LIFE_MS: 1000 * 60 * 60 * 24 * 3,
  MAX_TOOL_STEPS: 10,
  MAX_GRAPH_NODES: 20,
  MEMORY_PRESSURE_THRESHOLD: 15,
  TOOL_TIMEOUT_MS: 500          // F743-6: hard time limit per tool execution
};

// ============================================================
// 🗂️ ERROR TAXONOMY  (FIX-18)
// All error codes live here. Never use raw string literals at call sites.
// ============================================================
const ERROR_CODES = {
  // Input / request layer
  INVALID_INPUT_TYPE:          "invalid_input_type",
  MALFORMED_REQUEST:           "malformed_request_payload_or_size_rejected",
  SECURITY_SANDBOX_VIOLATION:  "security_sandbox_violation",
  MALFORMED_TOKEN_SEQUENCE:    "malformed_token_sequence",
  MATH_LOGIC_EXCEPTION:        "math_logic_exception",
  UNPARSED_TRAILING_TOKENS:    "unparsed_trailing_tokens",
  PARSER_RUNTIME_FAULT:        "parser_runtime_fault",
  // Executor / tool layer
  MALFORMED_STEP_SCHEMA:       "malformed_step_schema_rejected",
  UNKNOWN_ACTION:              "unknown_action_token",
  UNAUTHORIZED_TOOL:           "unauthorized_tool_execution_clearance",
  SANDBOX_VIOLATION:           "sandbox_isolation_violation",
  CUSTOM_EXEC:                 "custom_extension_execution",
  // Governor layer
  GOVERNOR_PRE_CANCEL:         "governor_pre_exec_cancel",
  GOVERNOR_CANCEL:             "governor_cancel",
  // Interaction layer
  INTERACTION_FAULT:           "interaction_layer_fault",
  // Router
  ROUTE_NOT_FOUND:             "route_not_found",
  // Phase 8
  REPLAN_HARD_STOP:            "replan_hard_stop_exceeded",
  // Phase 7.2
  RULE_VIOLATION:              "rule_violation",
  EMERGENCY_STOP:              "emergency_stop",
  // Phase 7.4
  INVALID_TOOL_PLUGIN:         "invalid_tool_plugin",
  TOOL_SCHEMA_MISMATCH:        "tool_schema_mismatch",
  TOOL_NETWORK_BLOCKED:        "tool_network_blocked",
  TOOL_RUNTIME_FAILURE:        "tool_runtime_failure",
  KERNEL_BLOCKED:              "governance_kernel_blocked",
  GRAPH_EXECUTION_FAULT:       "graph_execution_fault",
  // Phase 7.4.2
  TOOL_BUDGET_EXCEEDED:        "tool_budget_exceeded",
  GRAPH_LIMIT_EXCEEDED:        "graph_limit_exceeded",
  // Phase 7.4.3
  TOOL_TIMEOUT:                "tool_timeout",
  ROOT_UNAUTHORIZED:           "root_unauthorized",
  ROOT_INVALID_COMMAND:        "root_invalid_command",
  ROOT_ROLLBACK_UNAVAILABLE:   "root_rollback_unavailable",
  // Phase 7.5
  ROOT_RATE_LIMITED:           "root_rate_limited",
  SYSTEM_PAUSED:               "system_paused_by_root",
  TOOL_DISABLED_BY_ROOT:       "tool_disabled_by_root",
  MEMORY_LOCKED:               "memory_write_locked_by_root",
  // Phase 7.6.1 — Security
  ROOT_INVALID_SIGNATURE:      "root_invalid_signature",
  ROOT_EXPIRED_REQUEST:        "root_expired_request",
  ROOT_REPLAY_BLOCKED:         "root_replay_blocked",
  ROOT_SESSION_INVALID:        "root_session_invalid",
  ROOT_TEMP_LOCKED:            "root_temp_locked",
  ROOT_BLINDED:                "mother_blinded_by_root",
  // Phase 7.8 — Coherence
  SYSTEM_INCOHERENT:           "system_incoherent",
  COHERENCE_FAILURE:           "coherence_failure",
  EXECUTION_DAMPENED:          "execution_dampened",
  INTENT_TOO_RISKY:            "intent_too_risky",
  // Phase 7.9
  AFL_DISABLED:                "afl_disabled",
  IDENTITY_LOCKED:             "identity_profile_locked",
  LEUKOCYTE_THREAT:            "leukocyte_threat_detected",
  ROOT_INTRUSION:              "root_intrusion_detected",
  // Phase 7.9.1 / 8.0
  EXTERNAL_DISABLED:           "external_layer_disabled",
  EXTERNAL_UNAUTHORIZED:       "external_source_unauthorized",
  EXTERNAL_TIMEOUT:            "external_request_timeout",
};

// ============================================================
// #️⃣  FNV-1a 32-BIT FINGERPRINT  (FIX-17)
// Fast, deterministic hash for trace result comparison.
// Replaces fragile JSON.stringify equality checks in the Governor.
// ============================================================
function fnv32a(str) {
  let hash = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    hash ^= str.charCodeAt(i);
    hash = (hash * 0x01000193) >>> 0; // keep 32-bit unsigned
  }
  return hash.toString(16);
}

function traceFingerprint(traceEntry) {
  // Canonicalise by sorting keys so insertion-order variance doesn't affect the hash
  const action  = traceEntry?.step?.action  ?? "";
  const success = String(traceEntry?.success ?? "");
  // For result, sort keys to avoid false negatives from property-order differences
  let resultStr = "";
  try { resultStr = JSON.stringify(traceEntry?.result, Object.keys(traceEntry?.result ?? {}).sort()); }
  catch { resultStr = String(traceEntry?.result ?? ""); }
  return fnv32a(`${action}::${success}::${resultStr}`);
}

// ============================================================
// 🧱 1. SAFE MATH ENGINE: PURE STRUCTURAL RECURSIVE AST PARSER
// ============================================================

function evaluateArithmeticExpression(expr) {
  if (typeof expr !== "string") return { error: ERROR_CODES.INVALID_INPUT_TYPE };
  const normalized = expr.replace(/\s+/g, "");

  if (!/^[0-9+\-*/().]+$/.test(normalized)) {
    return { error: ERROR_CODES.SECURITY_SANDBOX_VIOLATION };
  }

  try {
    const tokens = normalized.match(/(\d+\.?\d*)|[\+\-\*\/\(\)]/g);
    if (!tokens || tokens.join("") !== normalized) return { error: ERROR_CODES.MALFORMED_TOKEN_SEQUENCE };

    let index = 0;

    function parsePrimary() {
      if (index >= tokens.length) throw new Error("Unexpected end of tokens");
      let token = tokens[index];
      if (token === "(") {
        index++;
        let val = parseExpression();
        if (index >= tokens.length || tokens[index] !== ")") throw new Error("Missing closing parenthesis");
        index++;
        return val;
      }
      if (/^[\d.]+$/.test(token)) {
        index++;
        const parsed = parseFloat(token);
        if (isNaN(parsed)) throw new Error("NaN token mismatch");
        return parsed;
      }
      throw new Error(`Unexpected token: ${token}`);
    }

    function parseMultiplicative() {
      let left = parsePrimary();
      while (index < tokens.length && (tokens[index] === "*" || tokens[index] === "/")) {
        let op = tokens[index++];
        let right = parsePrimary();
        if (op === "/") {
          if (right === 0) throw new Error("Division by zero intersection");
          left = left / right;
        } else {
          left = left * right;
        }
      }
      return left;
    }

    function parseExpression() {
      let left = parseMultiplicative();
      while (index < tokens.length && (tokens[index] === "+" || tokens[index] === "-")) {
        let op = tokens[index++];
        let right = parseMultiplicative();
        left = op === "+" ? left + right : left - right;
      }
      return left;
    }

    const finalVal = parseExpression();
    if (index < tokens.length) return { error: ERROR_CODES.UNPARSED_TRAILING_TOKENS };
    if (!isFinite(finalVal) || isNaN(finalVal)) return { error: ERROR_CODES.MATH_LOGIC_EXCEPTION };
    return { result: finalVal };
  } catch (err) {
    return { error: ERROR_CODES.PARSER_RUNTIME_FAULT, diagnostic: err.message };
  }
}

// ============================================================
// 💾 2. UNIFIED MEMORY STORAGE MODEL & ATOMIC WRITES
// ============================================================

function initializeUnifiedHive(existing = null) {
  const base = {
    lifecycleState: {
      goalId: null,
      objective: "",
      status: "PENDING",
      priority: 1,
      trackingTrace: []
    },
    cognitiveTrace: [],
    longTermMemory: [],
    replanCount: 0,
    depth: 0,
    governorNotes: [],
    version: 1,
    writeLock: 0,
    rootLog:      [],
    rootContext:  { notes: [], overrides: [], influence: 0 },
    stateHistory: [],
    // P75-10: Root Integration Layer state
    rootState: {
      systemPaused: false,
      forceMode:    null,
      toolOverrides: {},
      memoryLock:   false,
      safeMode:     false
    },
    rootQueue: [],
    rootMeta: {
      lastCommandTs: 0,
      commandCount:  0,
      burstWindow:   2000,
      maxBurst:      5
    },
    // S762/S763/S765: Security state
    rootSecurity: {
      usedNonces:     [],
      metrics:        { rapidCommands: 0, failedAuth: 0, signatureErrors: 0 },
      lockedUntil:    0,
      lastFailWindow: 0
    },
    rootSessions: [],
    // MT1: Mother Transcendence Core
    motherCore: {
      chronos:  { lastSync: Date.now(), drift: 0 },
      mimicry:  { active: false, borrowedTool: null, signatureHash: null, borrowCount: 0 },
      zenith:   { latencyThreshold: 1200, overclock: false },
      rootLens: { active: true, blinded: false }
    },
    motherState:      { lastDecisions: [] },  // MT16
    mother:           { activeSiphons: [], mimicryContext: null },  // MT11/MT12
    executionContext:  null,  // MT23: isolated per cycle
    lastStableState:  null,   // P85: coherence recovery snapshot
    autonomyEnabled:  true,   // ACE-1: coherence engine can disable autonomy
    // Phase 7.9
    afl: { enabled: true, mode: "normal", userOverride: null, rootOverride: null },
    identity: {
      profile: "polite", tone: "warm", verbosity: 0.8, expressiveness: 0.5,
      userOverride: null, rootOverride: null,
      consistencyScore: 1.0, drift: 0, memory: []
    },
    leukocytes: { activity: 1.0, detectedThreats: [], recycledEnergy: 0, lastSweep: Date.now() },
    awareness:  { lastUpdate: Date.now(), signals: [], environment: {}, active: true },
    lastUpdated: Date.now()
  };

  if (!existing) return base;

  return {
    ...base,
    ...existing,
    lifecycleState: existing.lifecycleState || base.lifecycleState,
    cognitiveTrace: Array.isArray(existing.cognitiveTrace) ? existing.cognitiveTrace.slice(-SYSTEM_CONTROL.MAX_TRACE_SIZE) : [],
    longTermMemory: Array.isArray(existing.longTermMemory) ? existing.longTermMemory.slice(-20) : [],
    governorNotes: Array.isArray(existing.governorNotes) ? existing.governorNotes.slice(-SYSTEM_CONTROL.MAX_TRACE_SIZE) : [],
    replanCount: typeof existing.replanCount === "number" ? existing.replanCount : 0,
    depth: typeof existing.depth === "number" ? existing.depth : 0,
    version: typeof existing.version === "number" ? existing.version : 1,
    writeLock: typeof existing.writeLock === "number" ? existing.writeLock : 0,
    rootLog:      Array.isArray(existing.rootLog) ? existing.rootLog.slice(-50) : [],
    rootContext:  existing.rootContext ?? { notes: [], overrides: [], influence: 0 },
    stateHistory: Array.isArray(existing.stateHistory) ? existing.stateHistory.slice(-10) : [],
    rootState:    existing.rootState ?? { systemPaused: false, forceMode: null, toolOverrides: {}, memoryLock: false, safeMode: false },
    rootQueue:    Array.isArray(existing.rootQueue) ? existing.rootQueue : [],
    rootMeta:     existing.rootMeta ?? { lastCommandTs: 0, commandCount: 0, burstWindow: 2000, maxBurst: 5 },
    rootSecurity: existing.rootSecurity ?? { usedNonces: [], metrics: { rapidCommands: 0, failedAuth: 0, signatureErrors: 0 }, lockedUntil: 0, lastFailWindow: 0 },
    rootSessions: Array.isArray(existing.rootSessions) ? existing.rootSessions.filter(s => s.expiresAt > Date.now()) : [],
    motherCore:   existing.motherCore ?? { chronos: { lastSync: Date.now(), drift: 0 }, mimicry: { active: false, borrowedTool: null, signatureHash: null, borrowCount: 0 }, zenith: { latencyThreshold: 1200, overclock: false }, rootLens: { active: true, blinded: false } },
    motherState:  existing.motherState ?? { lastDecisions: [] },   // MT16: Mother self-awareness
    mother:       existing.mother ?? { activeSiphons: [], mimicryContext: null }, // MT11/MT12
    executionContext: null,   // MT23: isolated per cycle, never persisted from previous
    lastStableState:  existing.lastStableState ?? null,
    autonomyEnabled:  existing.autonomyEnabled !== false,
    afl:         existing.afl      ?? { enabled: true, mode: "normal", userOverride: null, rootOverride: null },
    identity:    existing.identity ?? { profile: "polite", tone: "warm", verbosity: 0.8, expressiveness: 0.5, userOverride: null, rootOverride: null, consistencyScore: 1.0, drift: 0, memory: [] },
    leukocytes:  existing.leukocytes ?? { activity: 1.0, detectedThreats: [], recycledEnergy: 0, lastSweep: Date.now() },
    awareness:   existing.awareness  ?? { lastUpdate: Date.now(), signals: [], environment: {}, active: true }
  };
}

async function loadHiveState(env, userId, abortSignal) {
  let kvState = null;
  let neonState = null;

  try {
    const data = await env.STORAGE.get(`hive:${userId}`);
    if (data) kvState = JSON.parse(data);
  } catch (e) {
    console.error(`[Cache Read Error]: ${e.message}`);
  }

  if (env.NEON_DB_URL) {
    try {
      const res = await fetch(env.NEON_DB_URL, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          query: `SELECT data FROM hive_states WHERE user_id = $1 LIMIT 1`,
          params: [userId]
        }),
        signal: abortSignal
      });
      if (res.ok) {
        const body = await res.json();
        if (body.rows?.[0]?.data) {
          const rawData = body.rows[0].data;
          // FIX-3: Wrap Neon JSON.parse in try/catch to prevent crash on corrupted rows
          try {
            neonState = typeof rawData === "string" ? JSON.parse(rawData) : rawData;
          } catch {
            neonState = null;
          }
        }
      }
    } catch (e) {
      console.error(`[Neon Read Error]: ${e.message}`);
    }
  }

  if (kvState && neonState) {
    return (kvState.version || 0) >= (neonState.version || 0) ? kvState : neonState;
  }
  return kvState || neonState || null;
}

async function persistHiveState(env, userId, hive) {
  const writeTimestamp = Date.now();

  // FIX-11 + F72-4: KV atomic lock with 3-attempt retry + 50ms backoff
  // onlyIfAbsent is atomic but under edge bursts a single attempt can miss valid writes.
  const lockKey = `lock:${userId}`;
  let lockAcquired = false;
  for (let attempt = 0; attempt < 3; attempt++) {
    try {
      const acquired = await env.STORAGE.put(lockKey, String(writeTimestamp), {
        expirationTtl: 10,
        onlyIfAbsent: true
      });
      if (acquired !== null) { lockAcquired = true; break; }
      // Lock held — wait before retrying
      await new Promise(r => setTimeout(r, 50));
    } catch (e) {
      console.error(`[Lock Error]: KV unavailable on attempt ${attempt + 1} for user ${userId}: ${e.message}`);
      break; // KV itself is down — fail closed immediately
    }
  }
  if (!lockAcquired) {
    console.warn(`[Write Skipped]: Could not acquire lock for user ${userId} after 3 attempts.`);
    return;
  }

  try {
    // FIX-16: Increment version before writing. This acts as a CAS token.
    // loadHiveState already picks the higher version between KV and Neon,
    // so whichever store ends up ahead will win reconciliation on the next load.
    // If Neon fails, the retry_queue entry in KV serves as the rollback anchor —
    // syncOverflow will drain it back into the vector store, and the KV hive copy
    // remains the authoritative source until Neon catches up.
    hive.version = (hive.version || 0) + 1;
    hive.lastUpdated = writeTimestamp;
    const packet = JSON.stringify(hive);

    // Step 1: Write KV first (fast, local to edge) — this is the source-of-truth
    try {
      await env.STORAGE.put(`hive:${userId}`, packet);
    } catch (e) {
      console.error(`[KV Write Drop]: ${e.message}`);
      // KV failed — do NOT write Neon with a version that KV doesn't acknowledge.
      // The current in-flight version is lost; next request will load the previous
      // version from whichever store has it, which is safe.
      return;
    }

    // Step 2: Replicate to Neon (durable, cross-region backup)
    if (env.NEON_DB_URL) {
      try {
        const res = await fetch(env.NEON_DB_URL, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            query: `INSERT INTO hive_states (user_id, data, updated_at) VALUES ($1, $2, NOW())
                    ON CONFLICT (user_id) DO UPDATE SET data = EXCLUDED.data, updated_at = NOW()`,
            params: [userId, packet]
          })
        });
        if (!res.ok) throw new Error(`Neon Server HTTP_${res.status}`);
      } catch (e) {
        // FIX-16: Neon write failed — queue the packet for retry.
        // KV already has the authoritative copy so state is not lost.
        // FIX-20: TTL of 86400s (24h) prevents unbounded KV growth on sustained Neon outages.
        console.error(`[Neon State Divergence]: ${e.message}`);
        await env.STORAGE.put(
          `retry_queue:hive:${userId}:${writeTimestamp}`,
          packet,
          { expirationTtl: 86400 }
        ).catch(() => {});
      }
    }
  } finally {
    await env.STORAGE.delete(lockKey).catch(() => {});
  }
}

// ============================================================
// 🧠 3. CONTINUITY CONSERVATOR (COMPACTION PIPELINE)
// ============================================================

async function processStateCompaction(env, hive, abortSignal = null) {
  // FIX-10: guard cognitiveTrace against corrupted DB state before length check
  const safeTrace = Array.isArray(hive.cognitiveTrace) ? hive.cognitiveTrace : [];
  if (!env.AI || safeTrace.length < SYSTEM_CONTROL.COMPACTION_THRESHOLD) return;
  try {
    const historicalTraceData = safeTrace.map(t => `Step:${t.step?.action}->Success:${t.success}->Result:${JSON.stringify(t.result)}`);
    // FIX-14: pass abortSignal so compaction doesn't outlive the request timeout
    const runOptions = abortSignal ? { signal: abortSignal } : {};
    const runResponse = await env.AI.run(
      "@cf/facebook/bart-large-cnn",
      {
        text: `Condense this trace transaction array into an axiomatic performance state overview summary: ${JSON.stringify(historicalTraceData)}`,
        max_length: 90
      },
      runOptions
    );
    if (runResponse && runResponse.summary) {
      hive.longTermMemory.push({
        axiom: runResponse.summary,
        ts: Date.now()
      });
      hive.cognitiveTrace = safeTrace.slice(-3);
    }
  } catch (e) {
    console.error(`[Compactor Dropped Cycle]: ${e.message}`);
  }
}

// ============================================================
// 🔍 4. SEMANTIC STORAGE LOGIC & VECTOR NORMALIZATION
// ============================================================

async function embedPromptText(text, env, abortSignal) {
  if (!env.AI) return { vector: null, fallback: true };  // FIX-5: structured fallback
  try {
    const res = await env.AI.run("@cf/baai/bge-base-en-v1.5", { text }, { signal: abortSignal });
    const array = res.data?.[0];
    if (!Array.isArray(array) || array.length === 0 || isNaN(array[0])) {
      return { vector: null, fallback: true };  // FIX-5
    }
    return { vector: array, fallback: false };  // FIX-5: always return structured object
  } catch {
    return { vector: null, fallback: true };  // FIX-5
  }
}

async function queryVectorDatabase(env, vector, userId, abortSignal) {
  if (!vector || !env.UPSTASH_VECTOR_REST_URL) return [];
  try {
    const res = await fetch(`${env.UPSTASH_VECTOR_REST_URL}/query`, {
      method: "POST",
      headers: { Authorization: `Bearer ${env.UPSTASH_VECTOR_REST_TOKEN}`, "Content-Type": "application/json" },
      body: JSON.stringify({ namespace: userId, vector, topK: 3, includeMetadata: true }),
      signal: abortSignal
    });
    if (res.ok) {
      const payload = await res.json();
      const rawMatches = payload.result || payload.matches || [];
      return rawMatches.map(m => ({
        id: m.id,
        score: m.score,
        metadata: m.metadata || m.vector?.metadata || {}
      }));
    }
  } catch {}
  return [];
}

async function persistSemanticMemory(env, vector, payload, tag, userId) {
  if (!vector || !env.UPSTASH_VECTOR_REST_URL) return;
  const wrapped = { ...payload, tag, ts: Date.now() };
  try {
    const res = await fetch(`${env.UPSTASH_VECTOR_REST_URL}/upsert`, {
      method: "POST",
      headers: { Authorization: `Bearer ${env.UPSTASH_VECTOR_REST_TOKEN}`, "Content-Type": "application/json" },
      body: JSON.stringify({ namespace: userId, vectors: [{ id: safeGenerateUUID(), values: vector, metadata: wrapped }] })
    });
    if (!res.ok) throw new Error();
  } catch {
    if (env.NEON_DB_URL) {
      await fetch(env.NEON_DB_URL, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          query: `INSERT INTO overflow_buffer (data) VALUES ($1)`,
          params: [JSON.stringify({ userId, vector, payload: wrapped, tag, ts: Date.now() })]
        })
      }).catch(() => {});
    }
  }
}

// ============================================================
// 🧠 5. COGNITIVE PIPELINES: ANALYST & UNIFIED PLANNER
// ============================================================

async function analystBrain(inputPayload, semanticContext, hive) {
  let tone = "neutral";
  let isGoal = false;
  const text = inputPayload.input.toLowerCase();

  if (semanticContext && semanticContext.length > 0) {
    const historicalMemoryStream = JSON.stringify(semanticContext).toLowerCase();
    // FIX-8: tags are now lowercase to match detectTag() output
    if (historicalMemoryStream.includes("creative") || historicalMemoryStream.includes("#creative")) tone = "creative_assisted";
    if (historicalMemoryStream.includes("academic") || historicalMemoryStream.includes("#academic")) tone = "analytical_focused";
  }

  if (["fix", "build", "create", "solve", "improve", "learn"].some(w => text.includes(w))) {
    isGoal = true;
    hive.lifecycleState = {
      goalId: safeGenerateUUID(),
      objective: inputPayload.input,
      status: "ACTIVE",
      priority: text.includes("urgent") ? 3 : 1,
      trackingTrace: [{ status: "ACTIVE", ts: Date.now() }],
      goalVector: inputPayload._promptVector ?? null  // P8-2: store embedding for semantic drift
    };
  }

  return {
    action: inputPayload.action || (text.match(/[0-9+\-*/]/) ? "calculate" : "motivate"),
    input: inputPayload.input,
    tone,
    isGoal
  };
}

// UPG-8: plannerBrainV2 — replaces plannerBrain with tone/context-aware branching
function plannerBrainV2(intent, hive, branchPivotPrompt = null) {
  const steps = [];

  // Pivot recovery path — same as before
  if (branchPivotPrompt) {
    steps.push({ action: "motivate", input: `Pivot redirect applied: ${branchPivotPrompt}` });
    if (hive.lifecycleState.status === "ACTIVE") {
      steps.push({ action: "execute_goal", input: `RECOVERY: Process target parameter objectives for ${hive.lifecycleState.objective}` });
    }
    return steps;
  }

  // Pre-optimised path — if longTermMemory already has a matching summary, skip deep planning
  if (hive.longTermMemory.length > 0) {
    const matchingSummaries = hive.longTermMemory.filter(
      m => m.axiom && m.axiom.toLowerCase().includes(intent.action)
    );
    if (matchingSummaries.length > 0) {
      steps.push({ action: intent.action, input: intent.input, mode: "PRE_OPTIMIZED" });
      return steps;
    }
  }

  // UPG-8: Context-aware goal branching based on tone
  if (intent.isGoal) {
    steps.push({ action: "analyze_goal", input: intent.input });
    if (intent.tone === "analytical_focused") {
      // analytical tone → attempt calculation as second step
      steps.push({ action: "calculate", input: intent.input });
    } else {
      // default / creative → execute goal directly
      steps.push({ action: "execute_goal", input: intent.input });
    }
    return steps;
  }

  // Active lifecycle goal path
  if (hive.lifecycleState.status === "ACTIVE") {
    steps.push({ action: "analyze_goal", input: hive.lifecycleState.objective });
    steps.push({ action: "execute_goal", input: hive.lifecycleState.objective });
    return steps;
  }

  // Default single-step path
  steps.push({ action: intent.action, input: intent.input });
  return steps;
}

// ============================================================
// 🏛️ 6. MOTHER v10 — ADAPTIVE GOVERNANCE ENGINE (PHASE 7)
// ============================================================

// ============================================================
// 🔒 PHASE 7.2 — GOVERNANCE COMPLETION LAYER
// ============================================================

// P72-1: Policy rule set — single source of truth for what Mother allows
const MOTHER_RULES = {
  maxInputLength: 500,
  blockedActions: [
    "shutdown_system",
    "delete_all_memory"
  ],
  restrictedTools: {
    execute_goal: { minPriority: 2 }
  },
  outputFilters: {
    blockErrors: true,
    sanitizeText: true
  }
};

// P72-1: Rule enforcement — called before every execution in executorSandbox
function enforceRules(step, hive) {
  if (!step || !step.action) return { blocked: true, reason: "invalid_step" };

  if (MOTHER_RULES.blockedActions.includes(step.action)) {
    return { blocked: true, reason: "blocked_action" };
  }

  const rule = MOTHER_RULES.restrictedTools[step.action];
  if (rule && (hive.lifecycleState.priority ?? 1) < rule.minPriority) {
    return { blocked: true, reason: "insufficient_priority" };
  }

  if (typeof step.input === "string" && step.input.length > MOTHER_RULES.maxInputLength) {
    return { blocked: true, reason: "input_too_large" };
  }

  return { blocked: false };
}

// P72-2 + F72-5: Real confidence engine — 5-dimension score, stress-weighted
// Score is multiplied by replan and depth penalties so stress reduces confidence naturally
function computeConfidence(lifecycleState, lastStep, hive) {
  let score = 0;
  const input = lifecycleState?.objective ?? lastStep?.input ?? "";

  if (input.length > 10)                             score += 0.2;  // input clarity
  if (lastStep?.action)                              score += 0.2;  // action validity
  if ((hive.depth ?? 0) < 3)                        score += 0.2;  // depth safety
  if ((hive.replanCount ?? 0) === 0)                score += 0.2;  // replan safety
  if ((hive.longTermMemory?.length ?? 0) > 0)       score += 0.2;  // memory consistency

  // F72-5: stress penalties — system under load should report lower confidence
  score *= (1 - Math.min((hive.replanCount ?? 0) * 0.1, 0.5));
  score *= (1 - Math.min((hive.depth ?? 0) * 0.05, 0.3));

  return Math.min(Math.max(score, 0), 1.0);
}

// P72-3: Memory governance gate — Mother decides what enters long-term memory
// Rejects noise, errors, and spam before persistSemanticMemory is called
function approveMemoryWrite(entry) {
  if (!entry) return false;
  if (JSON.stringify(entry).length < 30) return false;  // too small to be useful
  if (entry.error) return false;                         // never memorise errors
  const text = (entry.input ?? "").toLowerCase();
  if (text.includes("test") || text.includes("random")) return false;  // reject spam
  return true;
}

// P72-4 + F72-2: Action modification — Mother mutates steps before execution if needed
// F72-2 fix: demote only when depth <= 2 to avoid locking system in analysis mode forever
function modifyStepIfNeeded(step, hive) {
  // Sanitize math input — strip any chars the AST parser doesn't accept
  if (step.action === "calculate") {
    step = { ...step, input: step.input.replace(/[^0-9+\-*/().]/g, "") };
  }

  // Demote aggressive execution under stress, BUT allow execution again at depth > 2
  // to prevent the system from getting permanently stuck in analysis mode
  if ((hive.replanCount ?? 0) > 2 && step.action === "execute_goal") {
    return (hive.depth ?? 0) > 2
      ? step  // F72-2: depth pressure overrides demotion — allow execution to proceed
      : { action: "analyze_goal", input: "Safe fallback mode activated" };
  }

  return step;
}

// P72-5: Emergency control — lightweight hard stop for runaway or overflow conditions
// Checked at the top of the executor loop before every step
function emergencyCheck(hive) {
  if ((hive.replanCount ?? 0) >= SYSTEM_CONTROL.MAX_REPLAN_HARD_STOP) {
    return { stop: true, reason: "runaway_loop" };
  }
  if ((hive.depth ?? 0) > SYSTEM_CONTROL.MAX_PLAN_DEPTH + 2) {
    return { stop: true, reason: "depth_overflow" };
  }
  return { stop: false };
}

// F72-3: Output sanitization — strips HTML/script tags from tool string outputs
// Prevents XSS payloads from propagating through the result pipeline
function sanitizeOutput(output) {
  if (output && typeof output.result === "string") {
    output = { ...output, result: output.result.replace(/<[^>]*>/g, "") };
  }
  return output;
}

// P8-1: Fingerprint-based repetition detection
// Uses pre-computed trace fingerprints (set by executorSandbox) instead of action names.
// Collision ratio over last 5 entries gives loopProbability 0→1.
// Falls back to action-name dedup if fingerprints are absent (old DB entries).
function detectLoopProbability(hive) {
  const trace = Array.isArray(hive.cognitiveTrace) ? hive.cognitiveTrace : [];
  const recent = trace.slice(-5);
  if (recent.length < 2) return 0;

  const fps = recent.map(t => t.fingerprint ?? traceFingerprint(t));
  const uniqueFps = new Set(fps);
  // collision ratio: 0 = all unique (safe), 1 = all identical (loop)
  return 1 - (uniqueFps.size / fps.length);
}

// P8-2: Intent drift with semantic vector similarity (when embeddings available)
// If the hive carries a goalVector (set at goal-creation time), computes cosine similarity
// between goal and result embeddings. Falls back to keyword scoring when vectors absent.
function cosineSimilarity(a, b) {
  if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return 0;
  let dot = 0, normA = 0, normB = 0;
  for (let i = 0; i < a.length; i++) {
    dot   += a[i] * b[i];
    normA += a[i] * a[i];
    normB += b[i] * b[i];
  }
  const denom = Math.sqrt(normA) * Math.sqrt(normB);
  return denom === 0 ? 0 : dot / denom;
}

function detectIntentDrift(hive, executionResult, resultVector = null) {
  // Semantic path — use cosine similarity when both vectors are available
  if (resultVector && Array.isArray(hive.lifecycleState.goalVector)) {
    const similarity = cosineSimilarity(hive.lifecycleState.goalVector, resultVector);
    return 1 - Math.max(0, Math.min(1, similarity)); // 0 aligned, 1 drifted
  }

  // Keyword fallback — same as Phase 7 but now clearly labelled as fallback
  const goal = hive.lifecycleState.objective?.toLowerCase() || "";
  const output = JSON.stringify(executionResult?.result ?? "").toLowerCase();
  const words = goal.split(/\s+/).filter(w => w.length > 3);
  if (words.length === 0) return 0;
  const hits = words.filter(w => output.includes(w)).length;
  return 1 - (hits / words.length);
}

// UPG-4: System stability index (0 = unstable, 1 = fully healthy)
function computeStability(hive) {
  const replanPenalty  = 1 - Math.min(hive.replanCount / SYSTEM_CONTROL.MAX_REPLAN_ATTEMPTS, 1);
  const depthPenalty   = 1 - Math.min(hive.depth / SYSTEM_CONTROL.MAX_PLAN_DEPTH, 1);
  const notesPenalty   = 1 - Math.min((hive.governorNotes?.length ?? 0) / 10, 1);
  return (replanPenalty + depthPenalty + notesPenalty) / 3;
}

// UPG-4: Risk score — combines error signal with depth pressure + memory failure density
function computeRisk(hive, executionResult) {
  let risk = 0;
  if (executionResult?.error) risk += 0.6;
  if (hive.depth >= SYSTEM_CONTROL.MAX_PLAN_DEPTH - 1) risk += 0.3;
  if (hive.replanCount >= SYSTEM_CONTROL.MAX_REPLAN_ATTEMPTS) risk += 0.1;
  // P74-4: memory failure density adds additional risk
  risk += memoryRiskInfluence(hive);
  return Math.min(risk, 1.0);
}

// UPG-6: Unified decision engine — single source of truth for all Governor decisions
function decideAction({ risk, loopProbability, drift, stability, phaseMode, hive }) {
  // PRE_EXEC: depth guard
  if (phaseMode === "PRE_EXEC") {
    if (hive.depth >= SYSTEM_CONTROL.MAX_PLAN_DEPTH) {
      return { action: "CANCEL", reason: "depth_limit_exhausted" };
    }
    if (stability < 0.3) {
      return { action: "CANCEL", reason: "system_instability" };
    }
    return { action: "PROCEED", reason: "pre_exec_clear" };
  }

  // POST_EXEC scored decisions — ordered by severity
  if (stability < 0.3) return { action: "CANCEL",  reason: "system_instability" };
  if (risk > 0.9)       return { action: "CANCEL",  reason: "high_risk" };
  if (loopProbability > 0.6) return { action: "REPLAN", reason: "loop_predicted" };
  if (drift > 0.7)      return { action: "REPLAN", reason: "intent_drift" };
  if (risk > 0.5)       return { action: "REPLAN", reason: "elevated_risk" };

  return { action: "PROCEED", reason: "nominal" };
}

// UPG-5: Learning feedback — write failure patterns to longTermMemory so Mother adapts
function learnFromFailure(hive, executionResult, governorDecision) {
  if (executionResult?.error || governorDecision.action !== "PROCEED") {
    hive.longTermMemory.push({
      type: "failure_pattern",
      error: executionResult?.error ?? null,
      reason: governorDecision.reason,
      action: governorDecision.action,
      depth: hive.depth,
      ts: Date.now()
    });
    // UPG-9: prune memory immediately after write to stay bounded
    pruneMemory(hive);
  }
}

// P8-4: Memory weighting + decay system
// Each entry is scored: failure_pattern entries count double (higher signal value),
// then decayed exponentially by age using MEMORY_DECAY_HALF_LIFE_MS.
// Entries whose decayed weight falls below 0.05 are pruned. Remaining capped at 20.
function computeMemoryWeight(entry) {
  const age = Date.now() - (entry.ts ?? 0);
  const halfLife = SYSTEM_CONTROL.MEMORY_DECAY_HALF_LIFE_MS;
  const decayFactor = Math.pow(0.5, age / halfLife); // exponential decay
  const typeWeight = entry.type === "failure_pattern" ? 2.0 : 1.0;
  return typeWeight * decayFactor;
}

function pruneMemory(hive) {
  if (!Array.isArray(hive.longTermMemory)) { hive.longTermMemory = []; return; }
  hive.longTermMemory = hive.longTermMemory
    .map(entry => ({ ...entry, _weight: computeMemoryWeight(entry) }))
    .filter(entry => entry._weight >= 0.05)   // drop fully decayed entries
    .sort((a, b) => b._weight - a._weight)     // highest weight first
    .slice(0, 20)
    .map(({ _weight, ...entry }) => entry);    // strip internal weight field before storing
}

// UPG-1: Mother v10 — 4-phase adaptive governance engine
// F741-2: Mother now receives kernel constraints and must respect them (hierarchical authority)
async function motherV10(hive, executionResult, phaseMode = "POST_EXEC", resultVector = null, kernel = null) {
  const stability       = computeStability(hive);
  const risk            = computeRisk(hive, executionResult);
  const loopProbability = detectLoopProbability(hive);
  const drift           = (phaseMode === "POST_EXEC" && executionResult)
    ? detectIntentDrift(hive, executionResult, resultVector)
    : 0;

  // F741-2: if kernel has set constraints, enforce them before Mother's own scoring
  if (kernel) {
    if (!kernel.allowExecution) {
      return {
        action: "CANCEL", reason: "kernel_blocked",
        riskScore: 1.0, confidenceScore: 0, stabilityIndex: stability,
        loopProbability, driftScore: drift,
        directivePrompt: "Governance kernel denied execution authority"
      };
    }
    if (kernel.constraints?.blockAutonomy && phaseMode === "AUTONOMY") {
      return {
        action: "CANCEL", reason: "kernel_autonomy_blocked",
        riskScore: 1.0, confidenceScore: 0, stabilityIndex: stability,
        loopProbability, driftScore: drift,
        directivePrompt: "Kernel blocked autonomy in current mode"
      };
    }
  }

  const decision = decideAction({ risk, loopProbability, drift, stability, phaseMode, hive });

  const assessment = {
    action:          decision.action,
    reason:          decision.reason,
    riskScore:       risk,
    // P72-2: real confidence engine replaces naive (1 - risk)
    confidenceScore: computeConfidence(hive.lifecycleState, hive.cognitiveTrace.at(-1)?.step ?? {}, hive),
    stabilityIndex:  stability,
    loopProbability,
    driftScore:      drift,
    directivePrompt: null
  };

  // Annotate directive prompt for upstream consumers
  if (decision.action === "CANCEL") {
    assessment.directivePrompt = `Mother v10 CANCEL [${decision.reason}]: stability=${stability.toFixed(2)} risk=${risk.toFixed(2)}`;
    hive.lifecycleState.status = "DEGRADED";
    hive.governorNotes.push({ type: "CANCEL", version: hive.version, assessment: { ...assessment } });
  } else if (decision.action === "REPLAN") {
    assessment.directivePrompt = `Mother v10 REPLAN [${decision.reason}]: loop=${loopProbability.toFixed(2)} drift=${drift.toFixed(2)}`;
    hive.governorNotes.push({ type: "REPLAN", version: hive.version, assessment: { ...assessment } });
  }

  // UPG-5: Learning phase — always runs on POST_EXEC
  if (phaseMode === "POST_EXEC") {
    learnFromFailure(hive, executionResult, decision);
  }

  return assessment;
}

// ============================================================
// ⚡ PHASE 7.4 — UNIFIED COGNITIVE EXECUTION SYSTEM (UCES)
// ============================================================

// -------------------------------------------------------
// P74-5: INTELLIGENCE BUS
// Single shared object assembled once per request cycle.
// Every module reads from this — Mother, Planner, ToolVM,
// Memory Runtime, and Autonomy Scheduler.
// -------------------------------------------------------
function computeIntelligenceBus(hive, extraSignals = {}) {
  const stability    = computeStability(hive);
  const loopProb     = detectLoopProbability(hive);
  const replanCount  = hive.replanCount ?? 0;
  const depth        = hive.depth ?? 0;

  // F742-5: multi-dimensional execution cost
  const trace        = Array.isArray(hive.cognitiveTrace) ? hive.cognitiveTrace : [];
  const avgLatency   = trace.length > 0
    ? trace.reduce((sum, t) => sum + (t.duration ?? 0), 0) / trace.length
    : 0;
  const latencyFactor      = (avgLatency * depth) / 1000;
  const memEntries         = (hive.memory?.longTerm ?? hive.longTermMemory ?? []).length;
  const memoryPressureFactor = Math.min(memEntries / 20, 1.0);  // 0→1 as memory fills
  const toolFailures       = (hive.memory?.failureMemory ?? []).length;
  const toolComplexityFactor = Math.min(toolFailures * 0.05, 0.5);
  const executionCost      = latencyFactor + memoryPressureFactor * 0.5 + toolComplexityFactor;

  return {
    risk:                Math.min((replanCount * 0.15) + (depth * 0.1) + (loopProb * 0.4), 1.0),
    stability,
    loopProbability:     loopProb,
    drift:               extraSignals.drift ?? 0,
    confidence:          computeConfidence(hive.lifecycleState, hive.cognitiveTrace?.at(-1)?.step ?? {}, hive),
    executionCost,
    memoryPressure:      memoryPressureFactor,
    mode:                hive.mode ?? "normal",
    replanCount,
    depth,
    rootContext:         hive.rootContext ?? { notes: [], overrides: [] }  // F743-5
  };
}

// -------------------------------------------------------
// P74-2: GOVERNANCE KERNEL
// Mother is now a system-wide policy engine, not just a
// decision brain. Kernel output gates every major action.
// -------------------------------------------------------
// F741-2: Governance Kernel is now the FINAL authority in the hierarchy.
// Intelligence Bus → Kernel → Mother → Planner → ToolVM
// Mother receives kernel constraints and must not override them.
// F741-5: kernel switches to "efficient" mode when execution cost exceeds threshold.
const EXECUTION_COST_THRESHOLD = 2.0; // seconds

function governanceKernel(bus) {
  // F741-5: escalate to efficient mode under high execution cost
  const effectiveMode = bus.executionCost > EXECUTION_COST_THRESHOLD
    ? "efficient"
    : bus.mode;

  return {
    allowExecution: bus.stability > 0.3,
    allowAutonomy:  bus.risk < 0.6 && effectiveMode !== "conservative" && effectiveMode !== "efficient",
    allowToolUse:   bus.confidence > 0.5,
    allowMemWrite:  bus.stability > 0.4,
    mode:           effectiveMode,
    // F741-2: explicit constraints Mother must respect
    constraints: {
      maxDepth:      SYSTEM_CONTROL.MAX_PLAN_DEPTH,
      maxReplans:    SYSTEM_CONTROL.MAX_REPLAN_ATTEMPTS,
      blockAutonomy: effectiveMode === "efficient" || effectiveMode === "conservative"
    }
  };
}

// -------------------------------------------------------
// P74-1: TOOL VM — Plugin Architecture
// Every tool is a versioned module with schema, permissions,
// and an isolated execute(). ToolVM() is the single runtime
// entry point — replaces direct BUILT_IN_CORE_TOOLS dispatch.
// -------------------------------------------------------
const TOOL_REGISTRY = {
  calculate: {
    id: "calculate", version: "1.0.0",
    schema: { input: "string", output: "number" },
    permissions: { memory: false, network: false },
    execute: async (input) => evaluateArithmeticExpression(input)
  },
  analyze_goal: {
    id: "analyze_goal", version: "1.0.0",
    schema: { input: "string", output: "string" },
    permissions: { memory: true, network: false },
    execute: async (input) => ({ result: `Analytical mapping validated for: ${input}` })
  },
  execute_goal: {
    id: "execute_goal", version: "1.0.0",
    schema: { input: "string", output: "string" },
    permissions: { memory: true, network: false },
    execute: async (input) => ({ result: `Execution committed for: ${input}` })
  },
  motivate: {
    id: "motivate", version: "1.0.0",
    schema: { input: "string", output: "string" },
    permissions: { memory: false, network: false },
    execute: async (input) => ({ result: `JARVIS UCES Active. ${input || ""}` })
  }
};

async function ToolVM(toolId, input, context, env) {
  // Resolve from built-in registry first, then KV custom tools
  let tool = TOOL_REGISTRY[toolId];

  if (!tool) {
    const customRegistry = await loadToolsFromStorage(env);
    const custom = customRegistry?.[toolId];
    if (custom) {
      // Wrap legacy KV tool in plugin shape
      tool = {
        id: toolId, version: "custom",
        schema: { input: "string", output: "string" },
        permissions: { memory: false, network: false, restricted: custom.restricted ?? false },
        execute: async () => ({ result: custom.logic ?? ERROR_CODES.CUSTOM_EXEC })
      };
    }
  }

  if (!tool || typeof tool.execute !== "function") {
    return { error: ERROR_CODES.INVALID_TOOL_PLUGIN };
  }

  // F743-2: respect Root DISABLE_TOOL command (TOOL_REGISTRY flag)
  if (tool._disabled) {
    return { error: ERROR_CODES.RULE_VIOLATION, detail: `Tool "${toolId}" is disabled by Root` };
  }

  // P75-9: respect rootState toolOverrides (per-request dynamic disable via RIL)
  if (context.hive?.rootState?.toolOverrides?.[toolId] === "disabled") {
    return { error: ERROR_CODES.TOOL_DISABLED_BY_ROOT, detail: `Tool "${toolId}" disabled by Root state` };
  }

  // Schema validation
  if (tool.schema && typeof input !== tool.schema.input) {
    return { error: ERROR_CODES.TOOL_SCHEMA_MISMATCH, expected: tool.schema.input };
  }

  // Permission: restricted tools need priority >= 3
  if (tool.permissions?.restricted && (context.priority ?? 1) < 3) {
    return { error: ERROR_CODES.UNAUTHORIZED_TOOL };
  }

  // Permission: network guard
  if (tool.permissions?.network === false && context.networkRequest) {
    return { error: ERROR_CODES.TOOL_NETWORK_BLOCKED };
  }

  // F742-1: execution budget — prevents tool-level CPU amplification (denial-of-service)
  context.executionCount = (context.executionCount ?? 0) + 1;
  if (context.executionCount > SYSTEM_CONTROL.MAX_TOOL_STEPS) {
    return { error: ERROR_CODES.TOOL_BUDGET_EXCEEDED, detail: `Exceeded ${SYSTEM_CONTROL.MAX_TOOL_STEPS} tool steps` };
  }

  // F741-1: memory write guard
  if (tool.permissions?.memory === false && context.memoryWrite) {
    return { error: ERROR_CODES.RULE_VIOLATION, detail: "memory_write_blocked" };
  }

  // F741-1: additional kernel-level gate — kernel must allow tool use
  if (context.kernel && !context.kernel.allowToolUse) {
    return { error: ERROR_CODES.KERNEL_BLOCKED, detail: "kernel_tool_use_denied" };
  }

  try {
    // F743-6: hard time limit per tool — prevents infinite loops and CPU burn inside tools
    const result = await Promise.race([
      tool.execute(input, context),
      new Promise((_, reject) =>
        setTimeout(() => reject(new Error("TOOL_TIMEOUT")), SYSTEM_CONTROL.TOOL_TIMEOUT_MS)
      )
    ]);
    return { ...result, _toolId: tool.id, _version: tool.version };
  } catch (e) {
    if (e.message === "TOOL_TIMEOUT") {
      return { error: ERROR_CODES.TOOL_TIMEOUT, detail: `Tool "${tool.id}" exceeded ${SYSTEM_CONTROL.TOOL_TIMEOUT_MS}ms` };
    }
    return { error: ERROR_CODES.TOOL_RUNTIME_FAILURE, detail: e.message };
  }
}

// -------------------------------------------------------
// P74-3: PLANNER BRAIN V3 — Execution Graph Builder
// Replaces the flat steps[] array with an explicit graph
// { nodes[], edges[] }. The executor walks edges in order.
// Maintains all plannerBrainV2 logic but outputs a graph.
// -------------------------------------------------------
function buildExecutionGraph(nodes) {
  // Build a simple linear DAG from an ordered node list
  return {
    nodes: nodes.map((n, i) => ({ id: `node_${i}`, ...n })),
    edges: nodes.slice(0, -1).map((_, i) => [`node_${i}`, `node_${i + 1}`])
  };
}

// F742-4: Contextual tool failure tracking — separates simple vs complex input profiles
// "simple" = input under 30 chars, "complex" = longer
function getToolFailureRate(hive, toolId, inputComplexity = "simple") {
  const toolMem = hive.memory?.toolMemory ?? [];
  const recent  = toolMem
    .filter(t => t.toolId === toolId && (t.complexity ?? "simple") === inputComplexity)
    .slice(-10);
  if (recent.length === 0) return 0;
  return recent.filter(t => !t.success).length / recent.length;
}

// F742-4 + MT21: select best tool — complexity-aware failure rate + trust score tiebreaker
function selectToolForGoal(hive, preferredAction, fallbackAction, inputText = "") {
  const complexity   = (inputText?.length ?? 0) > 30 ? "complex" : "simple";
  const failRate     = getToolFailureRate(hive, preferredAction, complexity);
  const trustScore   = computeToolTrust(preferredAction, hive);

  // If failure rate is high AND trust is negative, switch to fallback
  if (failRate > 0.5 && trustScore < 0) {
    console.warn(`[PlannerV3]: "${preferredAction}" ${complexity} failRate=${(failRate*100).toFixed(0)}% trust=${trustScore} → "${fallbackAction}"`);
    return fallbackAction;
  }
  return preferredAction;
}

function plannerBrainV3(intent, hive, branchPivotPrompt = null) {
  const nodeList = [];

  if (branchPivotPrompt) {
    nodeList.push({ type: "tool",      action: "motivate",     input: `Pivot: ${branchPivotPrompt}` });
    if (hive.lifecycleState?.status === "ACTIVE") {
      nodeList.push({ type: "tool",    action: "execute_goal", input: `RECOVERY: ${hive.lifecycleState.objective}` });
    }
    return buildExecutionGraph(nodeList);
  }

  // Pre-optimised path from memory
  if ((hive.memory?.longTerm ?? hive.longTermMemory ?? []).length > 0) {
    const mem = hive.memory?.longTerm ?? hive.longTermMemory;
    const match = mem.find(m => m.axiom?.toLowerCase().includes(intent.action));
    if (match) {
      nodeList.push({ type: "tool", action: intent.action, input: intent.input, mode: "PRE_OPTIMIZED" });
      return buildExecutionGraph(nodeList);
    }
  }

  if (intent.isGoal) {
    nodeList.push({ type: "cognitive", action: "analyze_goal", input: intent.input });
    const preferred  = intent.tone === "analytical_focused" ? "calculate" : "execute_goal";
    const toolAction = selectToolForGoal(hive, preferred, "analyze_goal", intent.input);
    nodeList.push({ type: "tool", action: toolAction, input: intent.input });
    nodeList.push({ type: "meta", action: "reflect", input: "self_reflection_pass" });
    return buildExecutionGraph(nodeList);
  }

  if (hive.lifecycleState?.status === "ACTIVE") {
    const goalTool = selectToolForGoal(hive, "execute_goal", "analyze_goal", hive.lifecycleState.objective);
    nodeList.push({ type: "cognitive", action: "analyze_goal",  input: hive.lifecycleState.objective });
    nodeList.push({ type: "tool",      action: goalTool,        input: hive.lifecycleState.objective });
    nodeList.push({ type: "meta",      action: "reflect",       input: "self_reflection_pass" });
    return buildExecutionGraph(nodeList);
  }

  nodeList.push({ type: "tool", action: intent.action, input: intent.input });
  return buildExecutionGraph(nodeList);
}

// Walk an execution graph — returns ordered flat step list (kept for backwards compat in autonomy cycle)
function flattenGraph(graph) {
  if (!graph?.nodes) return [];
  const nodeMap = Object.fromEntries(graph.nodes.map(n => [n.id, n]));
  const visited = new Set();
  const result  = [];
  let current = "node_0";
  while (current && !visited.has(current)) {
    visited.add(current);
    const node = nodeMap[current];
    if (node) result.push({ action: node.action, input: node.input, type: node.type, mode: node.mode });
    const nextEdge = graph.edges.find(([from]) => from === current);
    current = nextEdge ? nextEdge[1] : null;
  }
  return result;
}

// F741-3: Graph-native executor — walks nodes/edges directly without flattening.
// Returns { finalResult, stopped, stopReason } so the caller can branch on outcome.
// Enables future conditional edges and parallel branches without restructuring.
async function executeGraph(graph, hive, env, controller, kernel, intent, bus = null) {
  if (!graph?.nodes || !graph?.edges) return { finalResult: null, stopped: false };

  // F742-3: cap graph size to prevent recursive replan graph explosion
  if (graph.nodes.length > SYSTEM_CONTROL.MAX_GRAPH_NODES) {
    console.error(`[executeGraph]: Graph exceeded ${SYSTEM_CONTROL.MAX_GRAPH_NODES} nodes — aborting`);
    return {
      finalResult: { error: ERROR_CODES.GRAPH_LIMIT_EXCEEDED, detail: `Graph has ${graph.nodes.length} nodes` },
      stopped: true, stopReason: "graph_limit"
    };
  }

  // P75-8: systemPaused execution guard — Root can halt the system entirely
  if (hive.rootState?.systemPaused) {
    return {
      finalResult: { error: ERROR_CODES.SYSTEM_PAUSED, mode: "halted" },
      stopped: true, stopReason: "system_paused"
    };
  }

  const nodeMap = Object.fromEntries(graph.nodes.map(n => [n.id, n]));
  const visited = new Set();
  let current   = "node_0";
  let finalResult = null;
  let stopped = false;
  let stopReason = null;

  while (current && !visited.has(current)) {
    visited.add(current);
    const node = nodeMap[current];
    if (!node) break;

    const step = { action: node.action, input: node.input, type: node.type, mode: node.mode };

    // Abort check at each node
    if (controller.signal.aborted) {
      const partial = hive.cognitiveTrace.slice(-1);
      return {
        finalResult: { error: "request_timeout", partial: partial[0]?.result ?? null },
        stopped: true, stopReason: "timeout"
      };
    }

    // Emergency + hard stop checks
    const emergency = emergencyCheck(hive);
    if (emergency.stop) {
      hive.lifecycleState.status = "DEGRADED";
      hive.governorNotes.push({ type: "EMERGENCY_STOP", version: hive.version, reason: emergency.reason });
      return { finalResult: { error: ERROR_CODES.EMERGENCY_STOP, detail: emergency.reason }, stopped: true, stopReason: "emergency" };
    }
    if (hive.replanCount >= SYSTEM_CONTROL.MAX_REPLAN_HARD_STOP) {
      hive.lifecycleState.status = "DEGRADED";
      return { finalResult: { error: ERROR_CODES.REPLAN_HARD_STOP, detail: "Hard stop" }, stopped: true, stopReason: "hard_stop" };
    }

    const safeStep = modifyStepIfNeeded(step, hive);

    // MT11: safe siphon — prevents duplicate and stack abuse
    if (safeStep.action && hive.rootState?.allowSiphon) {
      motherSiphonToolSafe(hive, safeStep.action);
    }

    const execResult = await executorSandbox(safeStep, env, hive, kernel);

    // MT4: wraith erase — clean mimicry state after every node execution
    wraithErase(hive);

    // Embed result for semantic drift (F72-1)
    let resultVector = null;
    if (execResult?.result && typeof execResult.result === "string") {
      const re = await embedPromptText(execResult.result, env, controller.signal);
      resultVector = re.vector;
    }

    // F741-2: Mother receives kernel for hierarchical authority
    const postCheck = await motherV12(hive, execResult, "POST_EXEC", resultVector, kernel);

    // F742-7: decision arbiter resolves conflicts between all subsystems
    const arbitration = decisionArbiter(bus ?? computeIntelligenceBus(hive), kernel, postCheck, hive);

    // ACE-5: execution dampener — stops runaway execution before escalation
    const dampened = dampenExecution(bus ?? computeIntelligenceBus(hive), arbitration);
    const finalArbitration = dampened !== arbitration ? dampened : arbitration;

    if (finalArbitration.action === "CANCEL" || finalArbitration.action === "DEGRADED") {
      return {
        finalResult: { error: ERROR_CODES.GOVERNOR_CANCEL, detail: finalArbitration.reason, source: finalArbitration.source },
        stopped: true, stopReason: finalArbitration.action.toLowerCase()
      };
    }

    if (finalArbitration.action === "REPLAN") {
      hive.replanCount = Math.min(hive.replanCount + 1, SYSTEM_CONTROL.MAX_REPLAN_HARD_STOP);
      registerEvent(hive, "drift_detected", { reason: finalArbitration.reason, source: finalArbitration.source });
      snapshotState(hive);
      const newGraph = plannerBrainV3(intent, hive, finalArbitration.reason);
      return executeGraph(newGraph, hive, env, controller, kernel, intent, bus);
    }

    finalResult = execResult;

    // Follow next edge
    const nextEdge = graph.edges.find(([from]) => from === current);
    current = nextEdge ? nextEdge[1] : null;
  }

  return { finalResult, stopped, stopReason };
}

// -------------------------------------------------------
// P74-4: UNIFIED MEMORY RUNTIME
// hive.memory replaces flat longTermMemory as the primary
// structure. Typed namespaces allow each subsystem to store
// and retrieve context independently. Migration from legacy
// longTermMemory is handled transparently in initMemory().
// -------------------------------------------------------
function initMemory(hive) {
  if (!hive.memory) {
    hive.memory = {
      shortTerm:     [],   // current session context (last 5 steps)
      longTerm:      hive.longTermMemory ?? [],  // migrate legacy field
      vectorMemory:  [],   // semantic search results cache
      failureMemory: [],   // error patterns from learnFromFailure
      toolMemory:    []    // per-tool execution history
    };
  }
  return hive.memory;
}

function updateMemoryFromTrace(hive, execResult, toolId) {
  const mem = initMemory(hive);

  // Short-term: last 5 trace summaries
  const last = hive.cognitiveTrace.at(-1);
  if (last) {
    mem.shortTerm.push({ action: last.step?.action, success: last.success, ts: Date.now() });
    if (mem.shortTerm.length > 5) mem.shortTerm = mem.shortTerm.slice(-5);
  }

  // Tool memory: per-tool success/failure log with complexity context
  if (toolId) {
    const lastStep = hive.cognitiveTrace.at(-1)?.step;
    const complexity = (lastStep?.input?.length ?? 0) > 30 ? "complex" : "simple";
    mem.toolMemory.push({ toolId, success: !execResult?.error, complexity, ts: Date.now() });
    if (mem.toolMemory.length > 30) mem.toolMemory = mem.toolMemory.slice(-30);
  }

  // Failure memory: capture errors for Mother risk scoring
  if (execResult?.error) {
    mem.failureMemory.push({ error: execResult.error, toolId, ts: Date.now() });
    if (mem.failureMemory.length > 20) mem.failureMemory = mem.failureMemory.slice(-20);
  }

  // Keep longTermMemory in sync for backwards compatibility
  hive.longTermMemory = mem.longTerm;
}

// Memory influence on Mother risk — failure density increases risk score
function memoryRiskInfluence(hive) {
  const mem = hive.memory;
  if (!mem) return 0;
  const recentFailures = (mem.failureMemory ?? []).filter(f => Date.now() - f.ts < 60000).length;
  return Math.min(recentFailures * 0.1, 0.4); // cap at 0.4 additional risk
}

// -------------------------------------------------------
// P74-6: EVENT-DRIVEN AUTONOMY SCHEDULER
// Replaces the post-execution boolean gate from P73-1.
// Autonomy now triggers off specific hive.events rather
// than a shouldEnterAutonomy() check.
// -------------------------------------------------------
async function autonomyEventRouter(hive, env, abortSignal, kernel) {
  if (!kernel.allowAutonomy) return;

  const events = (hive.events ?? []).slice(-5); // only look at recent events

  for (const event of events) {
    if (abortSignal?.aborted) break;
    if (event._handled) continue; // skip already-processed events

    if (event.type === "execution_complete" && event.data?.success) {
      // Goal just completed successfully — run a follow-on autonomous cycle
      await runAutonomousCycle(hive, env, abortSignal);
      event._handled = true;
    }

    if (event.type === "drift_detected") {
      // Drift detected — trigger Mother rebalance via a forced replan note
      hive.governorNotes.push({
        type: "AUTONOMY_REBALANCE",
        note: "Drift event triggered autonomous rebalance",
        ts: Date.now()
      });
      event._handled = true;
    }

    if (event.type === "tool_failure") {
      // Tool failure — trigger recovery planner
      hive.governorNotes.push({
        type: "TOOL_RECOVERY",
        note: `Tool failure recovery triggered for: ${event.data?.toolId ?? "unknown"}`,
        ts: Date.now()
      });
      event._handled = true;
    }
  }
}

// ============================================================
// ⚡ 7. SANDBOXED TOOL ENGINE & VALIDATION LAYER
// ============================================================

// UPG-7: Pre-execution step validation — prevents garbage reaching execution
function validateStep(step) {
  if (!step || typeof step.action !== "string") return false;
  if (typeof step.input !== "string") return false;
  if (step.input.length > 500) return false;
  return true;
}

async function executorSandbox(step, env, hive, kernel = null) {
  const telemetry = { start: Date.now(), success: false };
  let output;

  // UPG-7: validate before any execution attempt
  if (!validateStep(step)) {
    output = { error: ERROR_CODES.MALFORMED_STEP_SCHEMA };
    return output;
  }

  // P72-1: policy enforcement — blocked actions, restricted tools, input length
  const ruleCheck = enforceRules(step, hive);
  if (ruleCheck.blocked) {
    output = { error: ERROR_CODES.RULE_VIOLATION, detail: ruleCheck.reason };
    return output;
  }

  try {
    // P74-1 + F741-1/2: All tool execution routes through ToolVM with kernel in context
    const context = {
      priority:       hive.lifecycleState?.priority ?? 1,
      networkRequest: false,
      memoryWrite:    false,
      kernel,          // F741-1/2: kernel enforces tool permissions hierarchically
      hive,            // P75-9: rootState.toolOverrides check inside ToolVM
      mode:           hive.mode ?? "normal"
    };

    if (step.action === "reflect") {
      // Meta node from PlannerV3 — runs selfReflect inline, no tool needed
      selfReflect(hive);
      output = { result: "self_reflection_complete" };
    } else {
      output = await ToolVM(step.action, step.input, context, env);
    }

    telemetry.success = !output?.error;
  } catch (e) {
    output = { error: ERROR_CODES.SANDBOX_VIOLATION, details: e.message };
    telemetry.success = false;
  }

  // F72-3: sanitize output before it enters the trace
  output = sanitizeOutput(output);

  // FIX-17: stamp fingerprint on each trace entry
  const entry = {
    step: JSON.parse(JSON.stringify(step)),
    result: JSON.parse(JSON.stringify(output)),
    success: telemetry.success,
    duration: Date.now() - telemetry.start
  };
  entry.fingerprint = traceFingerprint(entry);
  hive.cognitiveTrace.push(entry);

  // P74-4: update unified memory runtime after every execution
  updateMemoryFromTrace(hive, output, step.action);

  return output;
}

// BUILT_IN_CORE_TOOLS removed in Phase 7.4 — all tools now registered in TOOL_REGISTRY
// and executed through ToolVM(). See the UCES section above.

async function loadToolsFromStorage(env) {
  try {
    const raw = await env.STORAGE.get("tools");
    return raw ? JSON.parse(raw) : {};
  } catch {
    return {};
  }
}

// ============================================================
// 🔁 8. DATA SYNC OVERFLOW PUMP (RECOVERY INFRASTRUCTURE)
// ============================================================

async function syncOverflow(env, userId) {
  if (!env.NEON_DB_URL || !env.UPSTASH_VECTOR_REST_URL) return { migrated: 0 };
  try {
    const pullRes = await fetch(env.NEON_DB_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        query: `DELETE FROM overflow_buffer WHERE data->>'userId' = $1 RETURNING data`,
        params: [userId]
      })
    });
    if (!pullRes.ok) {
      console.error(`[syncOverflow]: Neon pull failed HTTP_${pullRes.status}`);
      return { migrated: 0 };
    }
    const body = await pullRes.json();
    const rows = body.rows || [];
    let migrated = 0;

    for (const row of rows) {
      // FIX-15: guard each row individually — a bad row must not abort the whole batch
      try {
        const target = row.data;
        if (!target?.vector) {
          console.warn(`[syncOverflow]: Row missing vector, skipping.`);
          continue;
        }
        // FIX-6 (retained): persistSemanticMemory is the correct function (storeVector never existed)
        await persistSemanticMemory(env, target.vector, target.payload ?? {}, target.tag ?? "#general", userId);
        migrated++;
      } catch (rowErr) {
        console.error(`[syncOverflow Row Error]: ${rowErr.message}`);
      }
    }

    return { migrated };
  } catch (e) {
    console.error(`[syncOverflow Fatal]: ${e.message}`);
    return { migrated: 0 };
  }
}

// ============================================================
// 🏷️  TAG CLASSIFIER (detectTag helper)
// ============================================================

function detectTag(input) {
  const text = input.toLowerCase();
  // FIX-8: emit lowercase tags so analystBrain semantic filters match correctly
  if (text.includes("essay") || text.includes("research") || text.includes("academic")) return "#academic";
  if (text.includes("poem") || text.includes("story") || text.includes("creative")) return "#creative";
  return "#general";
}

// ============================================================
// ⚖️  PHASE 7.4.2 — DECISION ARBITER (ROOT AUTHORITY)
// Single arbitration layer that resolves conflicts between all
// subsystems in strict priority order:
//   1. Kernel  (hard policy — always wins)
//   2. Mother  (adaptive decision)
//   3. toolMemory (learning signal)
//   4. Events  (reactive/predictive overrides)
// Prevents cross-system feedback loops by having one single
// place that determines the final action for any given cycle.
// ============================================================
function decisionArbiter(bus, kernel, motherDecision, hive) {
  // Priority 0: ROOT — absolute final authority, overrides everything
  // Root overrides are injected via processRootCommand into hive.rootContext.overrides
  const rootOverride = (hive.rootContext?.overrides ?? []).find(o => !o._consumed);
  if (rootOverride) {
    rootOverride._consumed = true;  // mark so it doesn't fire again next cycle
    hive.rootLog.push({ action: "ROOT_OVERRIDE", override: rootOverride, ts: Date.now() });
    return { action: rootOverride.action, source: "root", reason: rootOverride.reason ?? "root_override" };
  }

  // Priority 1: Kernel — hard safety policy
  if (!kernel.allowExecution) {
    return { action: "DEGRADED",  source: "kernel",      reason: "kernel_blocked_execution" };
  }
  if (kernel.constraints?.blockAutonomy && motherDecision?.action === "AUTONOMY") {
    return { action: "CANCEL",    source: "kernel",      reason: "kernel_blocked_autonomy" };
  }

  // Priority 2: Mother — adaptive intelligence
  if (motherDecision?.action === "CANCEL") {
    return { action: "CANCEL",    source: "mother",      reason: motherDecision.reason };
  }

  // Priority 3: toolMemory — reliability enforcement
  const toolMem    = hive.memory?.toolMemory ?? [];
  const recentTools = toolMem.slice(-5);
  const allFailing  = recentTools.length >= 3 && recentTools.every(t => !t.success);
  if (allFailing) {
    return { action: "REPLAN",    source: "toolMemory",  reason: "all_tools_failing" };
  }

  // Priority 4: Events — predictive reactions
  const recentEvents        = (hive.events ?? []).slice(-5);
  const hasFailurePredicted = recentEvents.some(e => e.type === "failure_predicted"         && !e._handled);
  const hasMemOverflow      = recentEvents.some(e => e.type === "memory_overflow_predicted" && !e._handled);
  const hasLoopPredicted    = recentEvents.some(e => e.type === "loop_predicted"            && !e._handled);
  if (hasLoopPredicted) {
    return { action: "REPLAN",    source: "events",      reason: "event_loop_predicted" };
  }
  if (hasFailurePredicted) {
    return { action: "REPLAN",    source: "events",      reason: "event_failure_predicted" };
  }
  if (hasMemOverflow) {
    pruneMemory(hive);
    return { action: "PROCEED",   source: "events",      reason: "memory_pruned_and_proceed" };
  }

  // Mother replan passes through if no higher authority overrode it
  if (motherDecision?.action === "REPLAN") {
    return { action: "REPLAN",    source: "mother",      reason: motherDecision.reason };
  }

  return { action: "PROCEED",     source: "arbiter",     reason: "nominal" };
}

// ============================================================
// 🤖 PHASE 7.3 — CONTROLLED AUTONOMY LAYER
// ============================================================

// P73-4: Adaptive behavior control — sets hive.mode based on system stress
// Applied early each request so all downstream logic can read hive.mode
function adjustBehavior(hive) {
  hive.mode = (hive.replanCount ?? 0) > 2 ? "conservative" : "normal";
}

// P73-5: Event system — capped ring buffer of 20 events stamped onto hive
// Foundation for future automation and observability
function registerEvent(hive, type, data) {
  if (!Array.isArray(hive.events)) hive.events = [];
  hive.events.push({ type, data, ts: Date.now() });
  if (hive.events.length > 20) hive.events = hive.events.slice(-20);
}

// P73-3: Self-reflection — observes last trace entry; logs failed steps to governorNotes
function selfReflect(hive) {
  const last = hive.cognitiveTrace.at(-1);
  if (!last) return;
  if (!last.success) {
    hive.governorNotes.push({
      type: "REFLECTION",
      note: `Last step [${last.step?.action}] failed — adjusting strategy`,
      ts: Date.now()
    });
  }
}

// P73-2: Task continuation — updates goal tracking trace after each execution
function updateGoalProgress(hive, result) {
  if (!hive.lifecycleState) return;
  if (result?.error) {
    hive.lifecycleState.status = "DEGRADED";
    hive.lifecycleState.trackingTrace?.push({ status: "DEGRADED", ts: Date.now() });
  } else {
    hive.lifecycleState.trackingTrace?.push({ status: "PROGRESS", ts: Date.now() });
  }
}

// P73-1: Autonomy gate — only enter autonomous cycle when system is clean and stable
function shouldEnterAutonomy(hive) {
  return (
    hive.lifecycleState?.status === "ACTIVE" &&
    (hive.replanCount ?? 0) === 0 &&
    (hive.depth ?? 0) < 2 &&
    hive.mode !== "conservative"   // never run autonomous cycle in conservative mode
  );
}

// P73-1: Autonomous cycle — generates follow-on steps from active goal
// Mother must PROCEED on every step; breaks immediately on any other decision
async function runAutonomousCycle(hive, env, abortSignal) {
  if (!shouldEnterAutonomy(hive)) return;

  const steps = [
    { action: "analyze_goal",  input: hive.lifecycleState.objective },
    { action: "execute_goal",  input: hive.lifecycleState.objective }
  ];

  for (const step of steps) {
    if (abortSignal?.aborted) break;

    const safeStep = modifyStepIfNeeded(step, hive);
    const result   = await executorSandbox(safeStep, env, hive);

    registerEvent(hive, "autonomous_step", { action: safeStep.action, success: !result?.error });
    selfReflect(hive);

    // Mother must approve every autonomous step — no exceptions
    const check = await motherV12(hive, result, "POST_EXEC");
    if (check.action !== "PROCEED") break;

    updateGoalProgress(hive, result);
  }
}

// ============================================================
// ⚡ 9. INTERACTION LAYER CONTROLLER (TIMEOUTS & ASYNC ISOLATION)
// ============================================================

async function processUserInteraction(req, env, ctx) {
  const rawBody = await safelyExtractPayload(req);
  if (!rawBody || !rawBody.input || rawBody.input.length > 2000) {
    return new Response(JSON.stringify({ error: ERROR_CODES.MALFORMED_REQUEST }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }

  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), SYSTEM_CONTROL.REQUEST_TIMEOUT_MS);

  try {
    const userIdPromise = getUserId(req, env);
    const activePromptVectorPromise = embedPromptText(rawBody.input, env, controller.signal);

    const userId = await userIdPromise;
    const { vector: activePromptVector, fallback: vectorFallback } = await activePromptVectorPromise;

    if (vectorFallback) {
      console.warn("[Embedding Degraded]: Running without semantic grounding.");
    }

    const rawHive = await loadHiveState(env, userId, controller.signal);
    const hive = initializeUnifiedHive(rawHive);

    // MT23: per-cycle execution isolation — new UUID for each request
    isolateExecutionContext(hive);

    // P74-4: initialise unified memory runtime
    initMemory(hive);

    ctx.waitUntil(syncOverflow(env, userId));

    // UPG-10: Semantic fallback injection
    let semanticContext = activePromptVector
      ? await queryVectorDatabase(env, activePromptVector, userId, controller.signal)
      : [];
    if (vectorFallback) {
      semanticContext = [{ metadata: { fallback: true, hint: rawBody.input } }];
    }

    // Pass the prompt vector into the body so analystBrain can store it as goalVector (P8-2)
    const enrichedBody = { ...rawBody, _promptVector: activePromptVector };
    const intent = await analystBrain(enrichedBody, semanticContext, hive);

    // P74-5: Build Intelligence Bus
    let bus = computeIntelligenceBus(hive);
    // IC-4: inject identity context into bus
    bus = injectIdentity(bus, hive);
    // MT14: adaptive zenith (multi-stage)
    applyZenithAdaptive(hive, bus);
    // MT20: cognitive load balancer
    balanceCognitiveLoad(bus, hive);
    // MT22: autonomous drift correction
    correctSystemDrift(bus, hive);
    // OL-10: leukocyte cycle — runs before coherence gate
    runLeukocyteSystem(hive, bus);
    // MT6: Prime directive — inject unconsumed Root override into Bus
    bus = applyPrimeDirective(hive, bus);

    // AFL: handle user-supplied afl mode from request body
    if (rawBody.afl?.mode && AFL_PROFILES[rawBody.afl.mode]) {
      hive.afl.userOverride = rawBody.afl.mode;
    }

    // Identity: handle user-supplied identity profile from request body
    if (rawBody.settings?.identity && IDENTITY_PROFILES[rawBody.settings.identity]) {
      hive.identity.userOverride = IDENTITY_PROFILES[rawBody.settings.identity];
    }

    // AFL-8 + MT19: apply adaptive mode from freedom score
    const freedom = computeFreedomScore(hive, bus);
    applyAdaptiveMode(hive, freedom);

    // P75-6: update safeMode based on current stability
    rootSafeGuard(bus, hive);
    // P75-7: clamp Root influence before it affects downstream decisions
    clampRootInfluence(hive);
    // P75-5: drain Root command queue BEFORE execution graph runs
    processRootQueue(hive, env);

    // P74-2: Governance Kernel — gates every major permission from one place
    const kernel = governanceKernel(bus);

    if (!kernel.allowExecution) {
      hive.mode = "degraded";
      hive.governorNotes.push({ type: "KERNEL_DEGRADED", version: hive.version, bus });
      await persistHiveState(env, userId, hive);
      return new Response(JSON.stringify({
        error: ERROR_CODES.KERNEL_BLOCKED, mode: "degraded",
        fallback: "light_response", detail: "Governance kernel blocked execution", bus
      }), { status: 503, headers: { "Content-Type": "application/json" } });
    }

    // P85: snapshot stable state before anything mutates
    snapshotSystemState(hive);

    // ACE-6: Coherence gate — sits between Kernel and Mother
    // Two-pass self-heal; hard REPLAN if still incoherent after both passes
    const coherenceBlock = coherenceGate(bus, hive);
    if (coherenceBlock) {
      hive.governorNotes.push({ type: "COHERENCE_BLOCK", version: hive.version, coherenceBlock });
      await persistHiveState(env, userId, hive);
      return new Response(JSON.stringify({ error: coherenceBlock.reason, detail: `Coherence: ${coherenceBlock.coherence?.toFixed(2)}` }), {
        status: 503, headers: { "Content-Type": "application/json" }
      });
    }

    // Mother v12 PRE_EXEC phase
    const preCheck = await motherV12(hive, null, "PRE_EXEC");
    if (preCheck.action === "CANCEL") {
      await persistHiveState(env, userId, hive);
      return new Response(JSON.stringify({ error: ERROR_CODES.GOVERNOR_PRE_CANCEL, detail: preCheck.directivePrompt }), {
        status: 429, headers: { "Content-Type": "application/json" }
      });
    }

    hive.depth++;
    adjustBehavior(hive);

    const execGraph = plannerBrainV3(intent, hive);

    // P84: validate execution intent before committing to graph
    const safePreCheck = validateExecutionIntent(preCheck, bus);
    if (safePreCheck?.action === "REPLAN") {
      hive.governorNotes.push({ type: "INTENT_BLOCKED", version: hive.version, reason: safePreCheck.reason });
    }

    const { finalResult } = await executeGraph(execGraph, hive, env, controller, kernel, intent, bus);
    let finalResultMut = finalResult;

    // MT12: guaranteed full siphon cleanup after entire graph execution
    enforceWraithCleanup(hive);

    await processStateCompaction(env, hive, controller.signal);

    // P86: post-execution stability check
    const busFinal = computeIntelligenceBus(hive);
    verifySystemStability(busFinal, hive);

    // IC-5/6/7: identity drift detection + stabilization + memory update
    const identityDrift = detectIdentityDrift(hive);
    stabilizeIdentity(hive, identityDrift);
    updateIdentityMemory(hive, { action: finalResultMut?.error ? "CANCEL" : "PROCEED" });

    // AFL-7: controlled execution override — if system blocked but freedom earned it
    if (finalResultMut?.error === ERROR_CODES.GOVERNOR_CANCEL || finalResultMut?.error === ERROR_CODES.COHERENCE_FAILURE) {
      if (allowControlledExecution(busFinal, freedom, hive)) {
        registerEvent(hive, "afl_controlled_override", { freedom, ts: Date.now() });
        // Don't override — log that AFL would have allowed it; let Root decide via /root/command
      }
    }

    // IC-8: apply identity tone to string result before returning
    if (finalResultMut?.result && typeof finalResultMut.result === "string") {
      finalResultMut = { ...finalResultMut, result: applyIdentityTone(finalResultMut.result, hive) };
    }

    selfReflect(hive);
    updateGoalProgress(hive, finalResultMut);
    registerEvent(hive, "execution_complete", { success: !finalResultMut?.error });
    if (finalResultMut?.error) registerEvent(hive, "tool_failure", { toolId: "unknown" });

    // F742-6 + P86: predictive events using already-computed busFinal
    if (busFinal.loopProbability > 0.6) {
      registerEvent(hive, "loop_predicted", { loopProbability: busFinal.loopProbability });
    }
    if (busFinal.risk > 0.7) {
      registerEvent(hive, "failure_predicted", { risk: busFinal.risk });
    }
    if (busFinal.memoryPressure > (SYSTEM_CONTROL.MEMORY_PRESSURE_THRESHOLD / 20)) {
      registerEvent(hive, "memory_overflow_predicted", { pressure: busFinal.memoryPressure });
    }

    await autonomyEventRouter(hive, env, controller.signal, kernel);

    const tag = detectTag(rawBody.input);
    // P75-10: memoryLock check + P72-3: Mother governance gate
    if (activePromptVector && !hive.rootState?.memoryLock && approveMemoryWrite({ input: rawBody.input, result: finalResultMut })) {
      ctx.waitUntil(persistSemanticMemory(env, activePromptVector, { input: rawBody.input, result: finalResultMut }, tag, userId));
    }

    await persistHiveState(env, userId, hive);

    return new Response(JSON.stringify({ result: finalResultMut, trace: hive.cognitiveTrace.slice(-3) }), {
      status: 200,
      headers: { "Content-Type": "application/json" }
    });

  } catch (err) {
    console.error(`[Interaction Fatal]: ${err.message}`);
    return new Response(JSON.stringify({ error: ERROR_CODES.INTERACTION_FAULT, detail: err.message }), {
      status: 500,
      headers: { "Content-Type": "application/json" }
    });
  } finally {
    clearTimeout(timeoutId);
  }
}

// ============================================================
// 🛠️ UTILITIES
// ============================================================

function safeGenerateUUID() {
  try {
    return crypto.randomUUID();
  } catch {
    return `fallback-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  }
}

async function safelyExtractPayload(req) {
  try {
    return await req.json();
  } catch {
    return null;
  }
}

async function getUserId(req, env) {
  const authHeader = req.headers.get("Authorization") || "";
  const token = authHeader.replace("Bearer ", "").trim();
  if (token) return token;
  return req.headers.get("CF-Connecting-IP") || "anonymous";
}

// ============================================================
// 🔐 PHASE 7.6.1 — ROOT SECURITY LAYER (S761–S768)
// Zero-trust hardening for all Root operations.
// Wraps existing Root system — does NOT change command logic.
// ============================================================

async function verifyRootSignature(token, timestamp, body, signature, env) {
  try {
    const secret  = env.ROOT_SECRET ?? env.ROOT_TOKEN ?? "jarvis-root-secret";
    const message = `${token}:${timestamp}:${body}`;
    const keyData = new TextEncoder().encode(secret);
    const msgData = new TextEncoder().encode(message);
    const key     = await crypto.subtle.importKey("raw", keyData, { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
    const sigBuf  = await crypto.subtle.sign("HMAC", key, msgData);
    const expected = Array.from(new Uint8Array(sigBuf)).map(b => b.toString(16).padStart(2, "0")).join("");
    return expected === signature;
  } catch { return false; }
}

function checkRootAbuse(hive, type = "failedAuth") {
  const sec = hive.rootSecurity;
  const now = Date.now();
  if (now - (sec.lastFailWindow ?? 0) > 10000) {
    sec.metrics = { rapidCommands: 0, failedAuth: 0, signatureErrors: 0 };
    sec.lastFailWindow = now;
  }
  sec.metrics[type] = (sec.metrics[type] ?? 0) + 1;
  if (sec.metrics.failedAuth > 5) {
    sec.lockedUntil = now + 30000;
    hive.rootState.safeMode = true;
    hive.rootLog.push({ type: "ROOT_LOCKED", reason: "abuse_detected", ts: now });
  }
}

function enforceSafeMode(hive, commandType) {
  const restricted = ["DELETE_MEMORY", "CLEAR_MEMORY", "ROLLBACK", "FORCE_EXECUTE"];
  if (hive.rootState?.safeMode && restricted.includes(commandType)) {
    return { blocked: true, reason: "safe_mode_active" };
  }
  return { blocked: false };
}

async function secureRootGate(request, env, hive) {
  const start     = Date.now();
  const ip        = request.headers.get("CF-Connecting-IP") ?? "unknown";
  const token     = request.headers.get("X-Root-Token")     ?? "";
  const timestamp = request.headers.get("X-Root-Timestamp") ?? "";
  const signature = request.headers.get("X-Root-Signature") ?? "";
  const nonce     = request.headers.get("X-Root-Nonce")     ?? "";
  const sessionId = request.headers.get("X-Root-Session")   ?? "";
  const sec       = hive.rootSecurity;

  if (Date.now() < (sec.lockedUntil ?? 0)) {
    return { authorized: false, response: new Response(JSON.stringify({ error: ERROR_CODES.ROOT_TEMP_LOCKED, unlocksAt: sec.lockedUntil }), { status: 429, headers: { "Content-Type": "application/json" } }) };
  }
  if (!token || token !== (env.ROOT_TOKEN ?? "jarvis-root")) {
    checkRootAbuse(hive, "failedAuth");
    return { authorized: false, response: new Response(JSON.stringify({ error: ERROR_CODES.ROOT_UNAUTHORIZED }), { status: 401, headers: { "Content-Type": "application/json" } }) };
  }
  const ts = parseInt(timestamp, 10);
  if (!ts || Math.abs(Date.now() - ts) > 10000) {
    checkRootAbuse(hive, "failedAuth");
    return { authorized: false, response: new Response(JSON.stringify({ error: ERROR_CODES.ROOT_EXPIRED_REQUEST }), { status: 401, headers: { "Content-Type": "application/json" } }) };
  }
  if (env.ROOT_SECRET && signature) {
    const rawBody  = await request.clone().text();
    const sigValid = await verifyRootSignature(token, timestamp, rawBody, signature, env);
    if (!sigValid) {
      checkRootAbuse(hive, "signatureErrors");
      return { authorized: false, response: new Response(JSON.stringify({ error: ERROR_CODES.ROOT_INVALID_SIGNATURE }), { status: 401, headers: { "Content-Type": "application/json" } }) };
    }
  }
  if (nonce) {
    if (sec.usedNonces.includes(nonce)) {
      return { authorized: false, response: new Response(JSON.stringify({ error: ERROR_CODES.ROOT_REPLAY_BLOCKED }), { status: 401, headers: { "Content-Type": "application/json" } }) };
    }
    sec.usedNonces.push(nonce);
    if (sec.usedNonces.length > 50) sec.usedNonces.shift();
  }
  const activeSessions = (hive.rootSessions ?? []).filter(s => s.expiresAt > Date.now());
  hive.rootSessions    = activeSessions;
  if (activeSessions.length > 0 && sessionId) {
    const session = activeSessions.find(s => s.id === sessionId);
    if (!session) {
      checkRootAbuse(hive, "failedAuth");
      return { authorized: false, response: new Response(JSON.stringify({ error: ERROR_CODES.ROOT_SESSION_INVALID }), { status: 401, headers: { "Content-Type": "application/json" } }) };
    }
  }
  return { authorized: true, sessionId, ip, latencyStart: start };
}

// ============================================================
// 🧠 PHASE 7.6.1 — MOTHER TRANSCENDENCE (MT1–MT8)
// ============================================================

function updateChronos(hive) {
  const now   = Date.now();
  const drift = now - (hive.motherCore?.chronos?.lastSync ?? now);
  if (hive.motherCore) hive.motherCore.chronos = { lastSync: now, drift };
  return drift;
}

function applyRootLens(hive) {
  if (!hive.motherCore) return null;
  if (hive.rootState?.void === true) {
    hive.motherCore.rootLens.blinded = true;
    hive.motherCore.rootLens.active  = false;
  }
  return hive.motherCore.rootLens.active ? null : { status: "MOTHER_BLINDED_BY_ROOT" };
}

function motherSiphonTool(toolId, hive) {
  if (!hive.motherCore) return null;
  if ((hive.motherCore.mimicry.borrowCount ?? 0) >= 3) return null; // MT8: borrow limit
  const tool = TOOL_REGISTRY[toolId];
  if (!tool) return null;
  const signatureHash = fnv32a(`${toolId}_${tool.version}_${Date.now()}`);
  hive.motherCore.mimicry = { active: true, borrowedTool: toolId, signatureHash, borrowCount: (hive.motherCore.mimicry.borrowCount ?? 0) + 1 };
  return { toolId, signatureHash };
}

function wraithErase(hive) {
  if (!hive.motherCore) return;
  hive.motherCore.mimicry = { active: false, borrowedTool: null, signatureHash: null, borrowCount: hive.motherCore.mimicry.borrowCount ?? 0 };
}

function applyZenithBuffer(hive, bus) {
  if (!hive.motherCore) return;
  const latencyMs = (bus.executionCost ?? 0) * 1000;
  hive.motherCore.zenith.overclock = latencyMs > hive.motherCore.zenith.latencyThreshold;
  if (hive.motherCore.zenith.overclock) hive.mode = "efficient";
}

function applyPrimeDirective(hive, bus) {
  const active = (hive.rootContext?.overrides ?? []).find(o => !o._consumed);
  if (active) { bus.rootDirective = active; active._consumed = true; }
  return bus;
}

// ============================================================
// 🎛  PHASE 7.9 — AFL + IDENTITY CORE v2 + OMNI-LEUKOCYTES v2
// Version: 7.9.0
// AFL = execution boldness control axis
// Identity = behavior/tone consistency axis
// Leukocytes = system survival/immune axis
// All three are parallel to authority chain, not part of it.
// ============================================================

const AFL_PROFILES = {
  safe:       { coherenceThreshold: 0.45, allowOverride: false, maxFreedom: 0.3 },
  normal:     { coherenceThreshold: 0.40, allowOverride: true,  maxFreedom: 0.7 },
  aggressive: { coherenceThreshold: 0.25, allowOverride: true,  maxFreedom: 1.0 }
};

function resolveAFLMode(hive) {
  if (hive.afl?.rootOverride === "off") { hive.afl.enabled = false; return "safe"; }
  if (hive.afl?.rootOverride)  return hive.afl.rootOverride;
  if (hive.afl?.userOverride)  return hive.afl.userOverride;
  return hive.afl?.mode ?? "normal";
}

function computeFreedomScore(hive, bus) {
  const profile     = AFL_PROFILES[resolveAFLMode(hive)] ?? AFL_PROFILES.normal;
  let score         = 0;
  const replanCount = (hive.motherState?.lastDecisions ?? []).filter(d => d.decision?.action === "REPLAN").length;
  if (replanCount >= 3)          score += 0.4;
  if (bus.confidence > 0.7)      score += 0.3;
  if (bus.loopProbability < 0.3) score += 0.3;
  return Math.min(profile.maxFreedom, score);
}

function breakOverDefensiveLoop(hive) {
  const lastThree = (hive.motherState?.lastDecisions ?? []).slice(-3);
  if (lastThree.length === 3 && lastThree.every(d => d.decision?.action === "REPLAN")) {
    return { action: "PROCEED", reason: "OVER_DEFENSE_BREAK", source: "AFL" };
  }
  return null;
}

function allowControlledExecution(bus, freedom, hive) {
  const profile = AFL_PROFILES[resolveAFLMode(hive)] ?? AFL_PROFILES.normal;
  if (!profile.allowOverride) return false;
  return freedom > 0.6 && bus.risk < 0.9;
}

function applyAdaptiveMode(hive, freedom) {
  if (freedom > 0.7) hive.mode = "responsive";
}

// Identity Core v2
const IDENTITY_PROFILES = {
  formal:   { tone: "calm",      verbosity: 0.7, expressiveness: 0.2 },
  polite:   { tone: "warm",      verbosity: 0.8, expressiveness: 0.5 },
  playful:  { tone: "energetic", verbosity: 0.9, expressiveness: 0.9 },
  direct:   { tone: "sharp",     verbosity: 0.4, expressiveness: 0.1 }
};

function resolveIdentity(hive) {
  if (hive.identity?.rootOverride) return hive.identity.rootOverride;
  if (hive.identity?.userOverride) return hive.identity.userOverride;
  return IDENTITY_PROFILES[hive.identity?.profile ?? "polite"] ?? IDENTITY_PROFILES.polite;
}

function injectIdentity(bus, hive) {
  const id = resolveIdentity(hive);
  return { ...bus, identity: { tone: id.tone, verbosity: id.verbosity, expressiveness: id.expressiveness, consistencyScore: hive.identity?.consistencyScore ?? 1.0 } };
}

function detectIdentityDrift(hive) {
  const last = (hive.identity?.memory ?? []).slice(-3);
  if (last.length < 3) return 0;
  const currentTone = resolveIdentity(hive).tone;
  return last.filter(d => d.tone !== currentTone).length / 3;
}

function stabilizeIdentity(hive, drift) {
  if (drift > 0.6) {
    hive.identity.tone             = "warm";
    hive.identity.profile          = "polite";
    hive.identity.userOverride     = null;
    hive.identity.consistencyScore = (hive.identity.consistencyScore ?? 1.0) * 0.9;
  }
}

function updateIdentityMemory(hive, decision) {
  if (!hive.identity) return;
  hive.identity.memory.push({ tone: resolveIdentity(hive).tone, action: decision?.action, timestamp: Date.now() });
  if (hive.identity.memory.length > 20) hive.identity.memory.shift();
}

function applyIdentityTone(result, hive) {
  if (typeof result !== "string") return result;
  const id = resolveIdentity(hive);
  let out  = result;
  if (id.tone === "warm")      out = "Alright — " + out;
  if (id.tone === "sharp")     out = out.replace(/\s+/g, " ").trim();
  if (id.tone === "energetic") out = out + " ✓";
  if (id.expressiveness > 0.7) out = out + " 🔥";
  if (id.verbosity < 0.5)      out = (out.split(".")[0] ?? out) + ".";
  return out;
}

// Omni-Leukocytes v2
const THREAT_LEVELS = { LOW: 0.3, MEDIUM: 0.6, HIGH: 0.85 };

function detectThreatsAdvanced(hive, bus) {
  const threats = [];
  if (bus.loopProbability > 0.7)    threats.push({ type: "LOOP",           level: "HIGH"   });
  if (bus.risk > 0.8)               threats.push({ type: "RISK_SPIKE",     level: "HIGH"   });
  if ((bus.executionCost ?? 0) > 3) threats.push({ type: "RESOURCE_STRESS",level: "MEDIUM" });
  if ((bus.drift ?? 0) > 0.5)       threats.push({ type: "DRIFT",          level: "MEDIUM" });
  return threats;
}

function leukocyteResponse(hive, threats) {
  for (const t of threats) {
    if (t.level === "HIGH") { hive.mode = "recovery"; if (hive.afl) hive.afl.enabled = false; }
    if (t.type === "LOOP")             hive.rootState.systemPaused = true;
    if (t.type === "RESOURCE_STRESS")  hive.mode = hive.mode === "recovery" ? "recovery" : "efficient";
  }
}

function autoHealSystem(hive) {
  if (hive.mode === "recovery") {
    if (hive.identity) hive.identity.drift = 0;
    hive.depth = Math.max(0, (hive.depth ?? 0) - 1);
  }
}

function enforceRootShield(hive, meta) {
  if (!meta?.verified || meta?.tampered) {
    if (!hive.rootState) hive.rootState = {};
    hive.rootState.locked = true;
    hive.mode             = "recovery";
    (hive.leukocytes.detectedThreats ??= []).push({ type: "ROOT_INTRUSION", level: "HIGH", ts: Date.now() });
    registerEvent(hive, "root_intrusion_detected", { ts: Date.now() });
  }
}

function detectResourceAnomaly(bus) {
  return (bus.executionCost ?? 0) > 4 && bus.confidence < 0.3;
}

function strengthenLeukocytes(hive, threats) {
  hive.leukocytes.activity = Math.min(1.5, (hive.leukocytes.activity ?? 1.0) + threats.length * 0.05);
}

function autoPhageCleanup(hive) {
  const lt = hive.memory?.longTerm;
  if (Array.isArray(lt) && lt.length > 100) {
    hive.memory.longTerm = lt.slice(-80);
    hive.longTermMemory  = hive.memory.longTerm;
  }
  hive.leukocytes.lastSweep = Date.now();
}

function runLeukocyteSystem(hive, bus) {
  if (!hive.leukocytes) hive.leukocytes = { activity: 1.0, detectedThreats: [], recycledEnergy: 0, lastSweep: Date.now() };
  const threats = detectThreatsAdvanced(hive, bus);
  leukocyteResponse(hive, threats);
  autoHealSystem(hive);
  autoPhageCleanup(hive);
  strengthenLeukocytes(hive, threats);
  hive.leukocytes.detectedThreats = threats;
  hive.leukocytes.recycledEnergy  = Math.min(1.0, (hive.leukocytes.recycledEnergy ?? 0) + threats.length * 0.1);
  if (detectResourceAnomaly(bus)) registerEvent(hive, "resource_anomaly_detected", { cost: bus.executionCost, confidence: bus.confidence, ts: Date.now() });
}

// ============================================================
// 🔬 PHASE 7.9.1 — LEUKOCYTE COMPLETIONS + EXTERNAL AWARENESS
// Version: 7.9.1
// ============================================================

// F791-1: Occupy idle cycles — cleanup when system is not actively executing
function occupyIdleCycles(hive) {
  if ((hive.cognitiveTrace?.length ?? 0) === 0) autoPhageCleanup(hive);
}

// F791-2: Logic energy recycling — converts failure count into tracked recycled energy
function recycleLogicEnergy(hive, failures) {
  const energy = (failures ?? 0) * 0.1;
  hive.leukocytes.recycledEnergy = Math.min(1.0, (hive.leukocytes.recycledEnergy ?? 0) + energy);
  return energy;
}

// F791-3: Named-string threat detection — simpler companion to detectThreatsAdvanced
function detectLogicThreats(hive, bus) {
  const threats = [];
  if (bus.loopProbability > 0.7)       threats.push("LOOP_CANCER");
  if ((bus.drift ?? 0) > 0.5)          threats.push("DRIFT_DECAY");
  if ((bus.executionCost ?? 0) > 3.5)  threats.push("RESOURCE_BLOAT");
  return threats;
}

function neutralizeThreats(hive, threats) {
  for (const t of threats) {
    if (t === "LOOP_CANCER")    hive.mode = "recovery";
    if (t === "RESOURCE_BLOAT") hive.depth = Math.max(0, (hive.depth ?? 0) - 1);
    if (t === "DRIFT_DECAY" && hive.identity) hive.identity.drift = 0;
  }
}

// F791-4: Root shield check — lightweight request meta verification
function rootShieldCheck(hive, requestMeta) {
  if (!requestMeta?.verified) {
    (hive.leukocytes.detectedThreats ??= []).push({ type: "UNAUTHORIZED_ACCESS", level: "HIGH", ts: Date.now() });
    registerEvent(hive, "root_intrusion_detected", { ts: Date.now() });
    return false;
  }
  return true;
}

// ============================================================
// 🌐 PHASE 8.0 FOUNDATION — EXTERNAL AWARENESS LAYER (P80)
// Cloudflare = brain/authority. External runtime (Koyeb) = execution/sensors.
// External runtime NEVER makes governance decisions — it only sends signals back.
// ============================================================

// P80-1: External config resolved from env at call time (no hardcoded values)
function getExternalConfig(env) {
  return {
    enabled:  !!(env.EXTERNAL_ENDPOINT),
    endpoint: env.EXTERNAL_ENDPOINT ?? "",
    apiKey:   env.EXTERNAL_API_KEY  ?? "",
    timeout:  1500
  };
}

// P80-2: Send task to external runtime
async function sendToExternal(task, hive, env) {
  const cfg = getExternalConfig(env);
  if (!cfg.enabled) return { error: ERROR_CODES.EXTERNAL_DISABLED };
  try {
    const ctrl = new AbortController();
    const tid  = setTimeout(() => ctrl.abort(), cfg.timeout);
    const res  = await fetch(`${cfg.endpoint}/task`, {
      method:  "POST",
      headers: { "Content-Type": "application/json", "Authorization": `Bearer ${cfg.apiKey}` },
      body: JSON.stringify({
        task,
        context: { mode: hive.mode, identity: { profile: hive.identity?.profile, tone: resolveIdentity(hive)?.tone }, memory: hive.memory?.shortTerm ?? [] }
      }),
      signal: ctrl.signal
    });
    clearTimeout(tid);
    if (!res.ok) return { error: ERROR_CODES.EXTERNAL_TIMEOUT, status: res.status };
    return await res.json();
  } catch (e) {
    return { error: ERROR_CODES.EXTERNAL_TIMEOUT, detail: e.message };
  }
}

// P80-3: Receive and normalise events from external runtime
async function handleExternalEvent(request, hive) {
  try {
    const body = await request.json();
    if (hive) registerAwarenessSignal(hive, { source: "external", event: body.event, payload: body.payload });
    return { type: "external_event", event: body.event, payload: body.payload };
  } catch {
    return { type: "external_event", error: "parse_failed" };
  }
}

// P80-5: Awareness signal intake — 50-entry capped ring buffer
function registerAwarenessSignal(hive, signal) {
  if (!hive.awareness) hive.awareness = { lastUpdate: Date.now(), signals: [], environment: {}, active: true };
  hive.awareness.signals.push({ ...signal, timestamp: Date.now() });
  hive.awareness.lastUpdate = Date.now();
  if (hive.awareness.signals.length > 50) hive.awareness.signals.shift();
}

// P80-6: Inject awareness context into Intelligence Bus
function injectAwareness(bus, hive) {
  if (!hive.awareness) return bus;
  return { ...bus, awareness: { signalCount: hive.awareness.signals.length, lastUpdate: hive.awareness.lastUpdate, active: hive.awareness.active } };
}

// P80-7: Offload decision — exceeds Worker limits → route to external runtime
function shouldOffload(bus, env) {
  if (!env?.EXTERNAL_ENDPOINT) return false;
  if ((bus.executionCost ?? 0) > 2.5) return true;
  if ((bus.depth ?? 0) > 5)           return true;
  if (bus.mode === "aggressive")       return true;
  return false;
}

// P80-8: Execution router — local graph vs external runtime
async function handleExecution(task, hive, bus, env, controller, kernel, intent) {
  if (shouldOffload(bus, env)) {
    registerEvent(hive, "task_offloaded", { task: task?.action, ts: Date.now() });
    return sendToExternal(task, hive, env);
  }
  const graph = { nodes: [{ id: "node_0", action: task.action, input: task.input, type: "tool" }], edges: [] };
  const { finalResult } = await executeGraph(graph, hive, env, controller, kernel, intent, bus);
  return finalResult;
}

// P80-9: Verify inbound external runtime callback
function verifyExternal(request, env) {
  const key = request.headers.get("X-External-Key");
  return key && key === (env.EXTERNAL_API_KEY ?? "");
}

// ============================================================
// ⚙️  PHASE 7.8 — SYSTEM COHERENCE LAYER + ACTIVE COHERENCE ENGINE
// Version: 7.8.0
// Authority chain: Root → Kernel → CoherenceGate → Mother → Planner → ToolVM
// ============================================================

function computeSystemCoherence(bus, hive) {
  let coherence = 1.0;
  if (bus.mode !== hive.mode)                                    coherence -= 0.2;
  if (bus.risk > 0.7 && hive.mode === "aggressive")             coherence -= 0.3;
  if ((bus.executionCost ?? 0) > 3 && hive.mode !== "recovery") coherence -= 0.2;
  if (bus.loopProbability > 0.6 && hive.autonomyEnabled)        coherence -= 0.15;
  return Math.max(0, coherence);
}

function unifySystemMode(bus, hive) {
  const signals = [
    (bus.executionCost ?? 0) > 3.5 ? "recovery"      : null,
    (bus.executionCost ?? 0) > 2.5 ? "stabilization" : null,
    bus.risk > 0.7                  ? "safe"          : null,
    hive.mode
  ].filter(Boolean);
  hive.mode = signals[0] ?? "normal";
}

function detectSystemConflict(bus, hive) {
  const conflicts = [];
  if (bus.risk > 0.7 && bus.confidence > 0.7)                       conflicts.push("RISK_CONFIDENCE_CONFLICT");
  if ((bus.executionCost ?? 0) > 3 && bus.loopProbability < 0.2)    conflicts.push("COST_LOOP_MISMATCH");
  if ((bus.drift ?? 0) > 0.5 && bus.stability > 0.8)                conflicts.push("DRIFT_STABILITY_PARADOX");
  if (conflicts.length > 0) registerEvent(hive, "system_conflict", { conflicts, ts: Date.now() });
  return conflicts;
}

function validateExecutionIntent(decision, bus) {
  if (decision?.action === "EXECUTE" && bus.risk > 0.85) {
    return { action: "REPLAN", reason: ERROR_CODES.INTENT_TOO_RISKY };
  }
  return decision;
}

function snapshotSystemState(hive) {
  try {
    hive.lastStableState = structuredClone({
      mode:      hive.mode,
      memory:    { longTermLength: hive.memory?.longTerm?.length ?? 0 },
      rootState: { systemPaused: hive.rootState?.systemPaused, memoryLock: hive.rootState?.memoryLock }
    });
  } catch { /* silent — structuredClone may fail on very large objects */ }
}

function verifySystemStability(bus, hive) {
  if (bus.stability < 0.3) {
    hive.mode = "recovery";
    registerEvent(hive, "system_instability_detected", { stability: bus.stability, ts: Date.now() });
  }
}

function autoCorrectSystem(bus, hive) {
  if ((bus.executionCost ?? 0) > 3 && hive.mode !== "recovery") hive.mode = "recovery";
  if (bus.risk > 0.8)            hive.mode = "safe";
  if (bus.loopProbability > 0.6) hive.autonomyEnabled = false;
  else if (hive.autonomyEnabled === false) hive.autonomyEnabled = true; // restore if clear
}

function runCoherencePass(bus, hive) {
  autoCorrectSystem(bus, hive);
  return computeSystemCoherence(bus, hive);
}

function stabilizeTools(hive) {
  for (const toolId of Object.keys(TOOL_REGISTRY)) {
    const rate = getToolFailureRate(hive, toolId, "simple");
    if (rate > 0.6) {
      if (!hive.rootState.toolOverrides) hive.rootState.toolOverrides = {};
      hive.rootState.toolOverrides[toolId] = "disabled";
      registerEvent(hive, "tool_auto_disabled", { toolId, failureRate: rate, ts: Date.now() });
    }
  }
}

function regulateMemoryPressure(hive) {
  const lt = hive.memory?.longTerm;
  if (Array.isArray(lt) && lt.length > 100) {
    hive.memory.longTerm = lt.slice(-50);
    hive.longTermMemory  = hive.memory.longTerm;
    registerEvent(hive, "memory_pruned", { prunedTo: 50, ts: Date.now() });
  }
}

function dampenExecution(bus, decision) {
  if ((bus.executionCost ?? 0) > 3 && decision?.action === "EXECUTE") {
    return { action: "REPLAN", reason: ERROR_CODES.EXECUTION_DAMPENED };
  }
  return decision;
}

// ACE-6: Main coherence gate — two-pass self-heal, then hard block
// AFL-5: threshold dynamically relaxed based on freedom score
function coherenceGate(bus, hive) {
  unifySystemMode(bus, hive);
  detectSystemConflict(bus, hive);
  stabilizeTools(hive);
  regulateMemoryPressure(hive);

  // AFL-5: relax threshold when system has earned freedom
  const freedom   = computeFreedomScore(hive, bus);
  const aflMode   = resolveAFLMode(hive);
  const profile   = AFL_PROFILES[aflMode] ?? AFL_PROFILES.normal;
  let threshold   = hive.afl?.enabled !== false ? profile.coherenceThreshold : 0.4;

  // AFL-6: break over-defensive loop before coherence check
  const defenseBreak = breakOverDefensiveLoop(hive);
  if (defenseBreak) return null; // override — force proceed

  let coherence = runCoherencePass(bus, hive);
  if (coherence < threshold + 0.1) coherence = runCoherencePass(bus, hive); // second chance

  if (coherence < threshold - 0.15) {
    return { action: "REPLAN", reason: ERROR_CODES.COHERENCE_FAILURE, coherence };
  }
  return null;
}

// ============================================================
// 🧠 PHASE 7.7 — MOTHER v12 + v13 STABILITY LAYERS (MT9–MT25)
// Predictive, self-regulating, system-aware sovereign intelligence.
// Authority chain strictly preserved: Root → Kernel → Mother → Planner → ToolVM
// ============================================================

// MT9: Self-validation — Mother checks her own decision before returning it
function motherSelfCheck(decision, bus) {
  if (!decision) return decision;
  if (bus.risk > 0.8 && decision.action === "EXECUTE") {
    return { ...decision, action: "REPLAN", reason: "HIGH_RISK_BLOCK" };
  }
  if (bus.loopProbability > 0.7) {
    return { ...decision, action: "CANCEL", reason: "LOOP_PREVENTED" };
  }
  return decision;
}

// MT10: Active chronos correction — not just tracking, but acting on drift
function stabilizeChronos(hive) {
  const drift = hive.motherCore?.chronos?.drift ?? 0;
  if (drift > 50) {
    hive.mode = "stabilization";
    registerEvent(hive, "chronos_drift_corrected", { drift, ts: Date.now() });
  }
}

// MT11: Siphon safety — prevents duplicate and stack siphon abuse
function canSiphon(hive, toolId) {
  const active = hive.mother?.activeSiphons ?? [];
  if (active.length >= 3)        return false;
  if (active.includes(toolId))   return false;
  return true;
}

function motherSiphonToolSafe(hive, toolId) {
  if (!canSiphon(hive, toolId)) return { status: "SIPHON_BLOCKED" };
  if (!Array.isArray(hive.mother.activeSiphons)) hive.mother.activeSiphons = [];
  hive.mother.activeSiphons.push(toolId);
  return motherSiphonTool(toolId, hive);
}

// MT12: Guaranteed full cleanup after graph execution
function enforceWraithCleanup(hive) {
  if (!hive.mother) return;
  hive.mother.activeSiphons  = [];
  hive.mother.mimicryContext  = null;
  wraithErase(hive);
}

// MT13: Hard Root Lens isolation — void state blocks all tool execution
function applyRootLensHard(hive) {
  if (hive.rootState?.void === true) {
    return { action: "CANCEL", reason: "ROOT_LENS_VOID", blockAllTools: true };
  }
  return null;
}

// MT14: Adaptive Zenith — multi-stage mode escalation (replaces binary MT5)
function applyZenithAdaptive(hive, bus) {
  const cost = bus.executionCost ?? 0;
  if (cost > 3.5)      hive.mode = "recovery";
  else if (cost > 2.5) hive.mode = "stabilization";
  else if (cost > 1.5) hive.mode = "efficient";
  if (hive.motherCore) hive.motherCore.zenith.overclock = cost > 1.5;
}

// MT15: Prime directive enforcement — Root override non-negotiable BEFORE Mother logic
function enforcePrimeDirective(bus, hive) {
  const overrides = hive.rootContext?.overrides ?? [];
  for (const o of overrides) {
    if (!o._consumed) {
      o._consumed = true;
      return { action: o.action, forced: true, source: "ROOT", reason: o.reason ?? "prime_directive" };
    }
  }
  return null;
}

// MT16: Mother state memory — logs her own decisions for self-awareness
function updateMotherState(hive, decision) {
  if (!hive.motherState) hive.motherState = { lastDecisions: [] };
  hive.motherState.lastDecisions.push({ decision, timestamp: Date.now() });
  if (hive.motherState.lastDecisions.length > 10) hive.motherState.lastDecisions.shift();
}

// MT17: Authority conflict resolver — clean Root→Kernel→Mother chain, no loops
function resolveAuthorityConflict(root, kernel, mother) {
  if (root?.forced)                       return root;
  if (kernel?.allowExecution === false)   return { action: "CANCEL", reason: "KERNEL_BLOCK" };
  return mother;
}

// MT18: Predictive outcome engine — simulate before committing
function simulateOutcome(bus) {
  let score = 0;
  if (bus.risk > 0.7)              score -= 2;
  if (bus.loopProbability > 0.6)   score -= 2;
  if (bus.confidence > 0.6)        score += 2;
  if ((bus.executionCost ?? 0) < 1.5) score += 1;
  return { safe: score >= 0, score };
}

// MT19: User friction model — quantifies frustration from recent decisions
function computeUserFriction(hive) {
  const recent = hive.motherState?.lastDecisions ?? [];
  let friction = 0;
  for (const d of recent) {
    if (d.decision?.action === "REPLAN") friction += 0.2;
    if (d.decision?.action === "CANCEL") friction += 0.3;
  }
  return Math.min(friction, 1.0);
}

// MT20: Cognitive load balancer — independent load signal alongside MT14
function balanceCognitiveLoad(bus, hive) {
  const cost = bus.executionCost ?? 0;
  if (cost > 3.5)      hive.mode = "recovery";
  else if (cost > 2.5) hive.mode = "stabilization";
  else if (cost > 1.5) hive.mode = "efficient";
}

// MT21: Tool trust index — weighted score from full toolMemory history
function computeToolTrust(toolId, hive) {
  const entries = (hive.memory?.toolMemory ?? []).filter(t => t.toolId === toolId);
  let score = 0;
  for (const e of entries) { score += e.success ? 1 : -2; }
  return score;
}

// MT22: Autonomous drift correction — system heals itself before degradation spreads
function correctSystemDrift(bus, hive) {
  if ((bus.drift ?? 0) > 0.5) {
    hive.mode = "stabilization";
    registerEvent(hive, "drift_corrected", { drift: bus.drift, ts: Date.now() });
  }
}

// MT23: Execution identity lock — per-cycle UUID isolation
function isolateExecutionContext(hive) {
  hive.executionContext = { id: safeGenerateUUID(), startedAt: Date.now(), isolated: true };
}

// MT24: Behavior pattern detection — prevents over-defensive stuck AI
function detectBehaviorPattern(hive) {
  const recent      = hive.motherState?.lastDecisions ?? [];
  const cancelCount = recent.filter(d => d.decision?.action === "CANCEL").length;
  return cancelCount >= 3 ? "over_defensive" : "normal";
}

// MT25: Final sovereign decision composer — single unified authority gate
function composeFinalDecision(root, kernel, mother) {
  if (root?.forced)                       return root;
  if (kernel?.allowExecution === false)   return { action: "CANCEL", reason: "KERNEL_BLOCK" };
  return mother;
}

// MT7 upgraded to MT25 — motherV12 integrates all MT layers in strict priority order
// Drop-in replacement for motherV11; all call sites updated below
async function motherV12(hive, executionResult, phaseMode = "POST_EXEC", resultVector = null, kernel = null) {
  // MT13: Hard lens check — if Root void, block everything including tools
  const hardLens = applyRootLensHard(hive);
  if (hardLens) return { ...hardLens, riskScore: 1, confidenceScore: 0, stabilityIndex: 0, loopProbability: 0, driftScore: 0, directivePrompt: hardLens.reason };

  // MT3: Soft lens check
  const lensBlock = applyRootLens(hive);
  if (lensBlock) return { action: "CANCEL", reason: ERROR_CODES.ROOT_BLINDED, riskScore: 1, confidenceScore: 0, stabilityIndex: 0, loopProbability: 0, driftScore: 0, directivePrompt: lensBlock.status };

  // MT2 + MT10: Chronos tracking + active correction
  updateChronos(hive);
  stabilizeChronos(hive);

  // Recompute bus with latest hive state (mode may have shifted)
  const liveBus = computeIntelligenceBus(hive);

  // MT15: Prime directive — Root override fires BEFORE Mother logic
  const primeOverride = enforcePrimeDirective(liveBus, hive);
  if (primeOverride) {
    updateMotherState(hive, primeOverride);
    return { action: primeOverride.action, reason: primeOverride.reason, source: "ROOT", riskScore: 0, confidenceScore: 1, stabilityIndex: 1, loopProbability: 0, driftScore: 0, directivePrompt: null };
  }

  // MT18: Predictive outcome — simulate before committing to execution
  const prediction = simulateOutcome(liveBus);
  if (!prediction.safe) {
    const unsafeDecision = { action: "REPLAN", reason: "PREDICTED_UNSTABLE_OUTCOME", score: prediction.score };
    updateMotherState(hive, unsafeDecision);
    return { ...unsafeDecision, riskScore: liveBus.risk, confidenceScore: liveBus.confidence, stabilityIndex: liveBus.stability, loopProbability: liveBus.loopProbability, driftScore: liveBus.drift, directivePrompt: null };
  }

  // MT24: Behavior pattern — break out of over-defensive loop
  const pattern = detectBehaviorPattern(hive);
  if (pattern === "over_defensive") {
    const forceProgress = { action: "PROCEED", reason: "FORCED_PROGRESS", source: "MT24" };
    updateMotherState(hive, forceProgress);
    return { ...forceProgress, riskScore: liveBus.risk, confidenceScore: liveBus.confidence, stabilityIndex: liveBus.stability, loopProbability: liveBus.loopProbability, driftScore: liveBus.drift, directivePrompt: null };
  }

  // MT19: User friction — adapt mode if frustration is high
  const friction = computeUserFriction(hive);
  if (friction > 0.6) hive.mode = "responsive";

  // Core Mother decision (motherV10 original logic)
  const coreDecision = await motherV10(hive, executionResult, phaseMode, resultVector, kernel);

  // MT9: Self-check — Mother validates her own decision
  const checkedDecision = motherSelfCheck(coreDecision, liveBus);

  // MT17/MT25: Authority conflict resolution → final decision
  const finalDecision = composeFinalDecision(null, kernel, checkedDecision);

  // MT16: Log decision to Mother state memory
  updateMotherState(hive, finalDecision);

  return finalDecision;
}

// ============================================================
// 👑 PHASE 7.4.3 — ROOT CONTROL ENGINE
// Root is the governance authority of last resort.
// Root does NOT mutate state directly — it issues structured
// commands that the system understands and records.
// Chain: Root → Kernel → Mother → toolMemory → Events
// ============================================================

// F743-1: Strict enumeration of all permitted Root commands
const ROOT_ACTIONS = {
  // Execution control
  OVERRIDE_EXECUTION: true,
  CANCEL_EXECUTION:   true,
  BREAK_LOOP:         true,
  MODIFY_STEP:        true,
  FORCE_PROCEED:      true,
  // System-level
  UPDATE_MOTHER_RULE: true,
  PATCH_KERNEL:       true,
  DISABLE_TOOL:       true,
  ENABLE_TOOL:        true,
  SET_MODE:           true,
  FREEZE_AUTONOMY:    true,
  RESUME_AUTONOMY:    true,
  // Memory
  DELETE_MEMORY:      true,
  EDIT_MEMORY:        true,
  VIEW_MEMORY:        true,
  // Recovery
  ROLLBACK:           true,
  // Phase 7.5 RIL
  PAUSE_SYSTEM:       true,
  RESUME_SYSTEM:      true,
  FORCE_EXECUTE:      true,
  CLEAR_MEMORY:       true,
  LOCK_MEMORY:        true,
  UNLOCK_MEMORY:      true,
  SIMULATE:           true,
  // Phase 7.9
  SET_AFL_MODE:            true,
  ENABLE_AFL:              true,
  DISABLE_AFL:             true,
  FORCE_AFL_MODE:          true,
  SET_IDENTITY_PROFILE:    true,
  LOCK_IDENTITY:           true,
  RESET_IDENTITY:          true
};

// F743-4: Snapshot hive before major mutations (rolling 10-entry history)
function snapshotState(hive) {
  try {
    const snap = structuredClone(hive);
    // Don't snapshot stateHistory inside the snapshot (infinite nesting)
    snap.stateHistory = [];
    if (!Array.isArray(hive.stateHistory)) hive.stateHistory = [];
    hive.stateHistory.push(snap);
    if (hive.stateHistory.length > 10) hive.stateHistory.shift();
  } catch (e) {
    console.warn(`[snapshotState]: Could not snapshot — ${e.message}`);
  }
}

// F743-4: Root rollback — rewind hive to N steps back
function rootRollback(hive, stepsBack = 1) {
  if (!Array.isArray(hive.stateHistory) || hive.stateHistory.length < stepsBack) {
    return { success: false, error: ERROR_CODES.ROOT_ROLLBACK_UNAVAILABLE };
  }
  const restored = hive.stateHistory[hive.stateHistory.length - stepsBack];
  // Restore all fields except stateHistory (keep the current one)
  const currentHistory = hive.stateHistory;
  Object.assign(hive, restored);
  hive.stateHistory = currentHistory;
  hive.rootLog.push({ action: "ROLLBACK", stepsBack, ts: Date.now(), reason: "root_rollback" });
  return { success: true, restoredVersion: hive.version };
}

// ============================================================
// 🔐 PHASE 7.5 — ROOT INTEGRATION LAYER (RIL)
// Secure, auditable control channel between the runtime and
// a future Root backend. Root commands flow through:
//   enqueue → validate → transaction → execute → log → rollback (if needed)
// processRootCommand is NOT replaced — it is called by the queue processor.
// ============================================================

// P75-1: Rate-limited command queue — prevents command flooding
function enqueueRootCommand(command, hive) {
  const now = Date.now();
  const meta = hive.rootMeta;

  if (now - meta.lastCommandTs < meta.burstWindow) {
    meta.commandCount++;
    if (meta.commandCount > meta.maxBurst) {
      hive.rootLog.push({ type: "RATE_LIMITED", command: command?.type, ts: now });
      return { error: ERROR_CODES.ROOT_RATE_LIMITED, detail: `Max ${meta.maxBurst} commands per ${meta.burstWindow}ms` };
    }
  } else {
    meta.commandCount  = 1;
    meta.lastCommandTs = now;
  }
  meta.lastCommandTs = now;

  if (!Array.isArray(hive.rootQueue)) hive.rootQueue = [];
  hive.rootQueue.push({
    id:      safeGenerateUUID(),
    command,
    ts:      now,
    status:  "pending"
  });

  return { queued: true, queueDepth: hive.rootQueue.length };
}

// P75-3: Lightweight transaction snapshot of fields Root commands can mutate
function beginRootTransaction(hive) {
  return {
    rootState: structuredClone(hive.rootState),
    memory:    structuredClone(hive.memory),
    mode:      hive.mode
  };
}

function rollbackRootTransaction(hive, snapshot) {
  hive.rootState = snapshot.rootState;
  hive.memory    = snapshot.memory;
  hive.mode      = snapshot.mode;
  // Keep longTermMemory in sync after rollback
  hive.longTermMemory = hive.memory?.longTerm ?? [];
}

// P75-4: Safe preview — returns risk/impact estimate without mutating state
function simulateRootCommand(command) {
  const riskMap = {
    DELETE_MEMORY:  { risk: 0.9, impact: "high" },
    CLEAR_MEMORY:   { risk: 0.9, impact: "high" },
    LOCK_MEMORY:    { risk: 0.6, impact: "medium" },
    DISABLE_TOOL:   { risk: 0.5, impact: "medium" },
    PAUSE_SYSTEM:   { risk: 0.4, impact: "medium" },
    BREAK_LOOP:     { risk: 0.3, impact: "medium" },
    SET_MODE:       { risk: 0.2, impact: "low" },
    FORCE_EXECUTE:  { risk: 0.5, impact: "medium" },
    ROLLBACK:       { risk: 0.4, impact: "medium" }
  };
  const profile = riskMap[command?.type] ?? { risk: 0.2, impact: "low" };
  return { preview: true, command: command?.type, ...profile };
}

// P75-5: Queue processor — drains rootQueue before execution graph runs
// Each item gets its own transaction; failures rollback without aborting the batch
function processRootQueue(hive, env) {
  if (!Array.isArray(hive.rootQueue) || hive.rootQueue.length === 0) return;

  const queue    = [...hive.rootQueue];
  hive.rootQueue = [];  // clear before processing so new commands during processing are queued separately

  for (const item of queue) {
    const { command } = item;
    const snapshot    = beginRootTransaction(hive);
    try {
      const result = processRootCommand(command, hive, env);
      item.status  = "executed";
      // Root log is already written inside processRootCommand — add queue trace
      hive.rootLog.push({ type: "QUEUE_EXEC", commandType: command.type, result, ts: Date.now() });
      registerEvent(hive, "root_action_applied", { commandType: command.type, result });
    } catch (err) {
      rollbackRootTransaction(hive, snapshot);
      item.status = "rolled_back";
      hive.rootLog.push({ type: "QUEUE_ROLLBACK", commandType: command.type, error: err.message, ts: Date.now() });
    }
  }

  // Cap log
  if (hive.rootLog.length > 50) hive.rootLog = hive.rootLog.slice(-50);
}

// P75-6: Safe mode guard — sets rootState.safeMode when system stability is low
function rootSafeGuard(bus, hive) {
  hive.rootState.safeMode = bus.stability < 0.3;
}

// P75-7: Clamp Root influence so context doesn't dominate the Bus
function clampRootInfluence(hive) {
  if (!hive.rootContext) return;
  if ((hive.rootContext.influence ?? 0) > 0.5) {
    hive.rootContext.influence = 0.5;
  }
}

// P75-10: applyRootAction — internal handler for RIL-specific action types
// Called by processRootCommand's switch for Phase 7.5 actions
function applyRootAction(action, hive) {
  switch (action.type) {
    case "PAUSE_SYSTEM":
      hive.rootState.systemPaused = true;
      break;
    case "RESUME_SYSTEM":
      hive.rootState.systemPaused = false;
      hive.rootState.forceMode    = null;
      break;
    case "FORCE_EXECUTE":
      hive.rootState.forceMode = "execute";
      // Inject a one-shot PROCEED override into arbiter
      if (!Array.isArray(hive.rootContext.overrides)) hive.rootContext.overrides = [];
      hive.rootContext.overrides.push({ action: "PROCEED", reason: "root_force_execute", _consumed: false });
      break;
    case "CLEAR_MEMORY":
      if (hive.memory) {
        hive.memory.shortTerm     = [];
        hive.memory.failureMemory = [];
        hive.memory.toolMemory    = [];
        if (!action.payload?.keepLongTerm) hive.memory.longTerm = [];
      }
      hive.longTermMemory = hive.memory?.longTerm ?? [];
      break;
    case "LOCK_MEMORY":
      hive.rootState.memoryLock = true;
      break;
    case "UNLOCK_MEMORY":
      hive.rootState.memoryLock = false;
      break;
    case "DISABLE_TOOL":
      hive.rootState.toolOverrides[action.target] = "disabled";
      break;
    case "ENABLE_TOOL":
      delete hive.rootState.toolOverrides[action.target];
      break;
  }
}

// F743-2: Root command processor — structured, logged, reversible (UNCHANGED from 7.4.3)
// P75-5 calls this; do NOT call directly from API endpoints — use enqueueRootCommand instead
  if (!command?.type || !ROOT_ACTIONS[command.type]) {
    return { success: false, error: ERROR_CODES.ROOT_INVALID_COMMAND };
  }

  // Snapshot before every mutation so it can be rolled back
  snapshotState(hive);

  // S768: safe mode restricts dangerous commands when security violations active
  const safeModeCheck = enforceSafeMode(hive, command.type);
  if (safeModeCheck.blocked) {
    hive.rootLog.push({ type: command.type, blocked: true, reason: safeModeCheck.reason, ts: Date.now() });
    return { success: false, error: safeModeCheck.reason };
  }

  // F743-3: every Root action is recorded to rootLog
  const logEntry = {
    action:  command.type,
    target:  command.target ?? null,
    payload: command.payload ?? null,
    ts:      Date.now(),
    reason:  command.reason ?? "manual_override"
  };

  let result = { success: true };

  switch (command.type) {
    case "BREAK_LOOP":
      hive.replanCount = 0;
      hive.depth       = 0;
      hive.lifecycleState.status = "ACTIVE";
      break;

    case "CANCEL_EXECUTION":
    case "OVERRIDE_EXECUTION":
    case "FORCE_PROCEED":
      // Inject a one-shot override into rootContext for decisionArbiter to consume
      if (!Array.isArray(hive.rootContext.overrides)) hive.rootContext.overrides = [];
      hive.rootContext.overrides.push({
        action:    command.type === "FORCE_PROCEED" ? "PROCEED" : (command.type === "CANCEL_EXECUTION" ? "CANCEL" : "PROCEED"),
        reason:    command.reason ?? command.type,
        _consumed: false
      });
      break;

    case "SET_MODE":
      hive.mode = command.payload?.mode ?? "normal";
      break;

    case "FREEZE_AUTONOMY":
      hive.rootContext.overrides.push({ action: "CANCEL", reason: "autonomy_frozen", _consumed: false });
      break;

    case "RESUME_AUTONOMY":
      // Clear any freeze overrides
      hive.rootContext.overrides = (hive.rootContext.overrides ?? []).filter(o => o.reason !== "autonomy_frozen");
      break;

    case "DISABLE_TOOL":
      if (TOOL_REGISTRY[command.target]) {
        TOOL_REGISTRY[command.target]._disabled = true;
      }
      break;

    case "ENABLE_TOOL":
      if (TOOL_REGISTRY[command.target]) {
        TOOL_REGISTRY[command.target]._disabled = false;
      }
      break;

    case "DELETE_MEMORY":
      if (command.target === "longTerm") {
        hive.memory.longTerm = (hive.memory.longTerm ?? []).filter(m => m.id !== command.payload?.id);
        hive.longTermMemory  = hive.memory.longTerm;
      } else if (command.target === "failureMemory") {
        hive.memory.failureMemory = [];
      } else if (command.target === "shortTerm") {
        hive.memory.shortTerm = [];
      }
      break;

    case "EDIT_MEMORY":
      hive.rootContext.notes.push({ note: command.payload?.note, ts: Date.now() });
      break;

    case "UPDATE_MOTHER_RULE":
      if (command.target && command.payload !== undefined) {
        MOTHER_RULES[command.target] = command.payload;
      }
      break;

    case "ROLLBACK":
      result = rootRollback(hive, command.payload?.stepsBack ?? 1);
      break;

    // P75-10: Phase 7.5 RIL actions — delegated to applyRootAction
    case "PAUSE_SYSTEM":
    case "RESUME_SYSTEM":
    case "FORCE_EXECUTE":
    case "CLEAR_MEMORY":
    case "LOCK_MEMORY":
    case "UNLOCK_MEMORY":
      applyRootAction(command, hive);
      break;

    // Phase 7.9 — AFL Root commands
    case "SET_AFL_MODE":
      if (AFL_PROFILES[command.payload?.mode]) {
        hive.afl.rootOverride = command.payload.mode;
        hive.afl.mode         = command.payload.mode;
      }
      break;
    case "ENABLE_AFL":
      hive.afl.enabled      = true;
      hive.afl.rootOverride = null;
      break;
    case "DISABLE_AFL":
      hive.afl.enabled      = false;
      hive.afl.rootOverride = "off";
      break;
    case "FORCE_AFL_MODE":
      hive.afl.rootOverride = command.payload?.mode ?? "normal";
      break;

    // Phase 7.9 — Identity Root commands
    case "SET_IDENTITY_PROFILE":
      if (IDENTITY_PROFILES[command.payload?.profile]) {
        hive.identity.rootOverride = IDENTITY_PROFILES[command.payload.profile];
        hive.identity.profile      = command.payload.profile;
      }
      break;
    case "LOCK_IDENTITY":
      hive.identity.rootOverride = resolveIdentity(hive); // lock current resolved identity
      break;
    case "RESET_IDENTITY":
      hive.identity.rootOverride = null;
      hive.identity.userOverride = null;
      hive.identity.profile      = "polite";
      hive.identity.drift        = 0;
      break;

    default:
      result = { success: false, error: ERROR_CODES.ROOT_INVALID_COMMAND };
  }

  hive.rootLog.push(logEntry);
  return result;
}

// ============================================================
// 🔀 ROUTER
// ============================================================

async function router(request, env, ctx, pathname) {
  if (pathname === "/interact" && request.method === "POST") {
    return processUserInteraction(request, env, ctx);
  }

  // S766: All /root/* routes go through secureRootGate
  if (pathname.startsWith("/root/")) {
    const userId   = request.headers.get("X-User-Id") ?? "anonymous";
    const abortCtl = new AbortController();
    const rawHive  = await loadHiveState(env, userId, abortCtl.signal);
    const hive     = initializeUnifiedHive(rawHive);
    initMemory(hive);

    // S763: session start is exempt from session requirement (but still needs token + signature)
    const isSessionStart = pathname === "/root/session/start" && request.method === "POST";

    const gate = await secureRootGate(request, env, hive);
    if (!gate.authorized) {
      // S767: log failed auth attempt with security context
      hive.rootLog.push({ type: "AUTH_FAILED", ip: request.headers.get("CF-Connecting-IP") ?? "unknown", ts: Date.now() });
      await persistHiveState(env, userId, hive).catch(() => {});
      return gate.response;
    }

    // S767: extend rootLog entry with security metadata
    const secMeta = { ip: gate.ip, sessionId: gate.sessionId, latency: Date.now() - gate.latencyStart };

    // S763: POST /root/session/start — issue 5-minute session
    if (isSessionStart) {
      const session = { id: safeGenerateUUID(), expiresAt: Date.now() + 300000, ip: gate.ip };
      hive.rootSessions.push(session);
      hive.rootLog.push({ type: "SESSION_START", sessionId: session.id, security: secMeta, ts: Date.now() });
      await persistHiveState(env, userId, hive);
      return new Response(JSON.stringify({ sessionId: session.id, expiresAt: session.expiresAt }), {
        status: 200, headers: { "Content-Type": "application/json" }
      });
    }

    // POST /root/command — enqueue a Root command (goes through rate-limit + queue)
    if (pathname === "/root/command" && request.method === "POST") {
      const body   = await safelyExtractPayload(request);
      const result = enqueueRootCommand(body, hive);
      // If not rate-limited, drain immediately so caller sees result in same request
      if (!result.error) {
        processRootQueue(hive, env);
      }
      await persistHiveState(env, userId, hive);
      return new Response(JSON.stringify({ result, rootLog: hive.rootLog.slice(-5) }), {
        status: result.error ? 429 : 200,
        headers: { "Content-Type": "application/json" }
      });
    }

    // POST /root/control — alias for /root/command (future Root panel UI endpoint)
    if (pathname === "/root/control" && request.method === "POST") {
      const body   = await safelyExtractPayload(request);
      const result = enqueueRootCommand(body, hive);
      if (!result.error) processRootQueue(hive, env);
      await persistHiveState(env, userId, hive);
      return new Response(JSON.stringify({ result, rootLog: hive.rootLog.slice(-5) }), {
        status: result.error ? 429 : 200,
        headers: { "Content-Type": "application/json" }
      });
    }

    // GET /root/simulate — preview a command's risk/impact without executing it
    if (pathname === "/root/simulate" && request.method === "GET") {
      const url     = new URL(request.url);
      const cmdType = url.searchParams.get("type") ?? "";
      const preview = simulateRootCommand({ type: cmdType });
      return new Response(JSON.stringify(preview), {
        status: 200, headers: { "Content-Type": "application/json" }
      });
    }

    // POST /root/rollback — rewind hive N steps
    if (pathname === "/root/rollback" && request.method === "POST") {
      const body   = await safelyExtractPayload(request);
      const result = rootRollback(hive, body?.stepsBack ?? 1);
      if (result.success) await persistHiveState(env, userId, hive);
      return new Response(JSON.stringify(result), {
        status: result.success ? 200 : 409,
        headers: { "Content-Type": "application/json" }
      });
    }

    // GET /root/view — inspect full system state
    if (pathname === "/root/view" && request.method === "GET") {
      return new Response(JSON.stringify({
        version:       hive.version,
        mode:          hive.mode,
        rootState:     hive.rootState,
        lifecycleState: hive.lifecycleState,
        rootLog:       hive.rootLog.slice(-20),
        rootContext:   hive.rootContext,
        rootQueue:     hive.rootQueue,
        rootMeta:      hive.rootMeta,
        events:        (hive.events ?? []).slice(-10),
        governorNotes: hive.governorNotes.slice(-10),
        memorySummary: {
          shortTerm:     hive.memory?.shortTerm?.length ?? 0,
          longTerm:      hive.memory?.longTerm?.length  ?? 0,
          failureMemory: hive.memory?.failureMemory?.length ?? 0,
          toolMemory:    hive.memory?.toolMemory?.length ?? 0
        },
        stateHistoryDepth: hive.stateHistory?.length ?? 0
      }), { status: 200, headers: { "Content-Type": "application/json" } });
    }

    return new Response(JSON.stringify({ error: ERROR_CODES.ROUTE_NOT_FOUND }), {
      status: 404, headers: { "Content-Type": "application/json" }
    });
  }

  if (pathname === "/health") {
    return new Response(JSON.stringify({ status: "ok", version: "7.9.0", build: "afl-identity-leukocytes" }), {
      headers: { "Content-Type": "application/json" }
    });
  }

  return new Response(JSON.stringify({ error: ERROR_CODES.ROUTE_NOT_FOUND }), {
    status: 404,
    headers: { "Content-Type": "application/json" }
  });
}
