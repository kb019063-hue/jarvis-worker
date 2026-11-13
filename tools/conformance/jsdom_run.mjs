// The reference side of the DOM scoreboard: runs corpus cases in jsdom.
//
// Reads one JSON object from standard input:
//   { "url": "...", "max_ms": 3000, "cases": [ { "html": "...", "code": "..." }, ... ] }
// and writes one JSON array to standard output, one entry per case:
//   { "output": "<what console.log printed>", "error": null | "<why the case cannot be used>" }
//
// Each case gets a new page. The page's own <script> elements run while it is
// parsed; the case's code then runs the way a deferred script does: the tree
// is complete, document.readyState is "interactive", DOMContentLoaded and
// load are still to come. After `load` the page runs until it has no timer
// or queued task left, then it is closed. A page that still has one after `max_ms` (an
// interval nobody clears) cannot be recorded: its output would depend on when
// it was cut off.
//
// Only console.log is output, and only primitives may be printed: an object
// is formatted differently by every engine, so a case that prints one is
// reported as an error instead of being recorded.
//
// Used by tools/conformance/run_dom_conformance.py (--reference, --record).
// jsdom is looked up in the directory given as the first argument.

import { createRequire } from 'node:module';
import path from 'node:path';

const jsdomDir = path.resolve(process.argv[2] || 'build/jsdom');
const require = createRequire(jsdomDir + path.sep);
const { JSDOM, VirtualConsole } = require('jsdom');

function readStdin() {
  return new Promise((resolve, reject) => {
    const chunks = [];
    process.stdin.on('data', (chunk) => chunks.push(chunk));
    process.stdin.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    process.stdin.on('error', reject);
  });
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// How many timers and queued tasks Node is holding, jsdom's own included: it
// uses timers for navigation and animation frames, and setImmediate for the
// tasks that fire hashchange and popstate. Called right after one of this
// file's sleeps has ended, so nothing counted here is ours.
const pendingWork = () => process.getActiveResourcesInfo()
  .filter((kind) => kind === 'Timeout' || kind === 'Immediate').length;

// jsdom has no `unhandledrejection` event: a promise a page rejects and never
// handles is Node's to report, and Node ends the process for it. Such a case
// cannot be recorded; say so and go on with the next one.
let escaped = null;
process.on('unhandledRejection', () => {
  escaped = escaped || 'a promise rejection nobody handled reached Node (jsdom has no unhandledrejection event)';
});
process.on('uncaughtException', (err) => {
  escaped = escaped || 'an exception escaped from jsdom: ' + String(err && err.message);
});

async function runCase(testCase, url, maxMs) {
  const lines = [];
  let error = null;
  escaped = null;
  const virtualConsole = new VirtualConsole();
  virtualConsole.on('log', (...args) => {
    const parts = [];
    for (const arg of args) {
      const type = typeof arg;
      if (arg === null || type === 'string' || type === 'number' || type === 'boolean' ||
          type === 'undefined' || type === 'bigint') {
        if (Object.is(arg, -0)) error = error || 'printed -0, which engines format differently';
        parts.push(String(arg));
      } else {
        error = error || 'printed something that is not a primitive (' + type + ')';
        parts.push('?');
      }
    }
    lines.push(parts.join(' '));
  });
  // Uncaught exceptions and "not implemented" notices arrive as jsdomError.
  // They are not output. A notice means the case leans on something jsdom
  // does not have, and what jsdom then does is not a reference for anything.
  virtualConsole.on('jsdomError', (err) => {
    const message = String(err && err.message);
    if (/not implemented/i.test(message)) {
      error = error || 'uses something jsdom does not implement: ' + message.split('\n')[0];
    }
  });

  let dom;
  try {
    dom = new JSDOM(testCase.html, {
      url,
      runScripts: 'dangerously',
      pretendToBeVisual: true,
      virtualConsole,
    });
  } catch (err) {
    return { output: '', error: 'the page could not be created: ' + String(err && err.message) };
  }
  const window = dom.window;
  try {
    window.eval(testCase.code);
  } catch (err) {
    const name = err && err.name ? err.name : typeof err;
    const message = err && err.message ? err.message : String(err);
    error = error || 'the code threw ' + name + ': ' + message;
  }
  // Wait for the page to finish loading, then for its timers to run out.
  const deadline = Date.now() + maxMs;
  while (window.document.readyState !== 'complete' && Date.now() < deadline) await sleep(1);
  if (window.document.readyState !== 'complete') error = error || 'the page never finished loading';
  for (let quiet = 0; quiet < 2;) {
    await sleep(1);
    quiet = pendingWork() === 0 ? quiet + 1 : 0;
    if (Date.now() > deadline) {
      error = error || 'a timer was still pending after ' + maxMs + ' ms (an interval that is never cleared?)';
      break;
    }
  }
  try { window.close(); } catch (err) { /* nothing to do */ }
  await sleep(0);
  return { output: lines.join('\n'), error: error || escaped };
}

const request = JSON.parse(await readStdin());
const results = [];
for (const testCase of request.cases) {
  results.push(await runCase(testCase, request.url, request.max_ms ?? 3000));
}
process.stdout.write(JSON.stringify(results));
