#pragma once

#include "litebird/js/JsValue.hpp"

namespace litebird {
namespace dom {

/// Begin delivery to a host target that owns its listener list rather than
/// using the DOM EventTarget listener store. The event has target/currentTarget
/// set to `target`, AT_TARGET phase, and the supplied trust state until ended.
bool begin_event_dispatch_at_target(const JsValue& event, const JsValue& target, bool trusted);
/// End target delivery. The event keeps target but clears currentTarget,
/// eventPhase and its transient path/flags.
void end_event_dispatch_at_target(const JsValue& event);
/// Whether stopImmediatePropagation() was called during target delivery.
bool event_immediate_stopped(const JsValue& event);

} // namespace dom
} // namespace litebird
