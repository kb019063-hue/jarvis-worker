# Finding 210 — the worker case was unasserted

**Resolved by Phase 206** (`docs/phases/206-the-codecs-inside-a-worker-scope.md`).

## 1. What was found

`AGENTS.md`'s `TextDecoder` row ended with "no suite asserts either global
inside a worker", and it had ended that way since Phase 163. The engine was
already right — `install_text_codec_builtins()` runs from `JsInterpreter`'s
setup, and a worker builds its own interpreter — so the row was reading
*unasserted* as *unfinished*. That is worth its own phase rather than a
sentence: with no suite, the next `if (m_document)` in front of that call
removes both globals from every worker and nothing goes red.

## 2. What was done

`text_codecs_are_installed_in_a_worker_scope` runs nineteen probes inside a
classic worker, through its own global, and answers them with `postMessage`.
The window's global is asked first, so a failure names the scope. The expected
answers are Node v22.22.3's line for line, and the whole answer is compared.

## 3. Evidence

`make test` **355/355**. Guarding the install with `if (m_document)` and
rebuilding makes the suite print nineteen differing lines and fail on its
assertion — `typeof => undefined,undefined` against `function,function` — and
restoring the call makes it pass. No engine file is changed by this phase.

## 4. What is still open

No `TextDecoderStream`/`TextEncoderStream` in any scope, the `<meta charset>`
prescan's single pass, and no suite for a module or a nested worker; the
per-interpreter install makes those the same code path, which is an argument
and not a test.
