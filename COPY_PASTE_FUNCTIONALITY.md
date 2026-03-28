# Copy/Paste in the ImGui/Emscripten/SDL2 Browser App

## Paste (system → ImGui) — Working

### Layer 1 — SDL eats the paste shortcut

SDL2's Emscripten backend registers a `keydown` listener on the canvas that calls
`e.preventDefault()` on every key event. This cancels the browser's native paste
action, so no `paste` event ever fires.

**Fix:** a capture-phase `window` keydown listener that calls
`e.stopImmediatePropagation()` for Ctrl+V and Shift+Insert. SDL's canvas listener
never runs, so it can't call `preventDefault()`, and the browser fires the `paste`
event normally.

### Layer 2 — clipboard text needs no permission

Inside a trusted `paste` event, `e.clipboardData.getData('text/plain')` is available
with no permission prompt. We store the result in `globalThis._pendingPaste` and
consume it from C++ each frame via `js_take_pending_paste()`.

Note: `navigator.clipboard.readText()` was tried and rejected — it triggers Chrome's
interactive clipboard permission UI even when the permission is pre-granted.

### Layer 3 — ImGui ignores characters while Ctrl is held

`AddInputCharactersUTF8` adds text to `ImGuiIO::InputQueueCharacters`. However,
`InputTextEx` in `imgui_widgets.cpp` only processes this queue when no Ctrl modifier
is held (`is_ctrl_down` check). Since Ctrl is still physically held when the paste
event fires (it arrives asynchronously after the keydown), injecting immediately
puts characters into a queue that ImGui silently discards.

**Fix:** buffer the paste text in `AppState::paste_pending` and inject it in the
first subsequent frame where `!io.KeyCtrl`. This causes a small but acceptable
delay after releasing Ctrl.

---

## Copy (ImGui → system) — Partially Working

### What should work but doesn't: ImGui's built-in copy routing

ImGui's `InputTextEx` handles Ctrl+C via `Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, 0, id)`
(imgui_widgets.cpp line ~5090). This goes through ImGui's shortcut routing system
(`SetShortcutRouting` / `CalcRoutingScore`). When it succeeds it calls
`ImGui::SetClipboardText()` → `PlatformIO.Platform_SetClipboardTextFn`.

**Confirmed working:**
- ImGui sees Ctrl+C (`IsKeyPressed(ImGuiKey_C) && io.KeyCtrl` = true)
- `io.KeyMods == ImGuiMod_Ctrl` matches exactly (no spurious modifiers)
- `io.WantTextInput = 1` (an InputText is active)
- `PlatformIO.Platform_SetClipboardTextFn` is set (PlatformFn=1)

**Confirmed not working:** `Platform_SetClipboardTextFn` is never called.

**Root cause (suspected, not fully traced):** ImGui's `Shortcut()` routing system
has a one-frame delay — routes are submitted for the next frame and tested against
the current frame's `RoutingCurr`. Additionally, the `is_copy` condition requires
`state->HasSelection()` for multiline InputText. One or both of these fail silently
in the SDL2/Emscripten context. The exact failure point was not isolated because the
internal routing state (`RoutingCurr`) is not accessible without modifying ImGui
source.

**Note on clipboard API:** ImGui 1.91.1+ moved clipboard hooks from `ImGuiIO` to
`ImGuiPlatformIO`. The old `io.SetClipboardTextFn` / `io.GetClipboardTextFn` are
no-ops in newer versions. Use `ImGui::GetPlatformIO().Platform_SetClipboardTextFn`
and `Platform_GetClipboardTextFn` instead.

### Workaround: detect copy with `IsKeyPressed`, copy full buffer

Since `IsKeyPressed(ImGuiKey_C)` works even though `Shortcut()` doesn't, we detect
Ctrl+C ourselves and copy the entire SCAD buffer. Selection-based copy is not
implemented (the selection state is inside ImGui's internal `ImGuiInputTextState`
and is not exposed via the public API).

```cpp
// ImGui's Shortcut() routing silently fails for InputText copy (Ctrl+C) in
// the SDL2/Emscripten backend. Detect it directly and copy the SCAD buffer.
if (ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl
        && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt
        && ImGui::GetIO().WantTextInput)
    js_set_clipboard(g_app.scad_buf);
```

`js_set_clipboard` calls `navigator.clipboard.writeText()` (async, best-effort).
On localhost and HTTPS this succeeds silently. On HTTP it may fail silently.

---

## Final Implementation

### `web/shell.html`

```js
// Capture paste text — no permission required inside a trusted paste event.
window.addEventListener('paste', function(e) {
  var text = e.clipboardData && e.clipboardData.getData('text/plain');
  if (text) globalThis._pendingPaste = text;
  e.preventDefault();
});

// Capture-phase keydown: stopImmediatePropagation for paste shortcuts so SDL
// never sees them and cannot call preventDefault() to block the paste event.
window.addEventListener('keydown', function(e) {
  var ctrl  = e.ctrlKey || e.metaKey;
  var shift = e.shiftKey;
  var k     = e.key;

  // Also let common browser shortcuts through before SDL eats them.
  var isBrowserShortcut =
    k === 'F12' || k === 'F5' ||
    (ctrl && shift && (k === 'I' || k === 'i' || k === 'J' || k === 'j' || k === 'C' || k === 'c')) ||
    (ctrl && (k === 'r' || k === 'R' || k === 'l' || k === 'L'));
  if (isBrowserShortcut) { e.stopImmediatePropagation(); return; }

  var isPaste = (ctrl && (k === 'v' || k === 'V') && !e.repeat) ||
                (shift && k === 'Insert' && !e.repeat);
  if (isPaste) e.stopImmediatePropagation();
}, { capture: true });
```

### `src/main.cpp` (per-frame, after `ImGui::NewFrame()`)

```cpp
// Paste: buffer incoming text (arrives while Ctrl is still held).
char* paste_text = js_take_pending_paste();
if (paste_text) {
    g_app.paste_pending = paste_text;
    free(paste_text);
}
// Inject once Ctrl is released — ImGui ignores InputQueueCharacters while
// any Ctrl modifier is held (it assumes Ctrl+key = shortcut, not text input).
if (!g_app.paste_pending.empty() && !ImGui::GetIO().KeyCtrl) {
    ImGui::GetIO().AddInputCharactersUTF8(g_app.paste_pending.c_str());
    g_app.paste_pending.clear();
}

// Copy: ImGui's Shortcut() routing fails silently; detect with IsKeyPressed instead.
if (ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl
        && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt
        && ImGui::GetIO().WantTextInput)
    js_set_clipboard(g_app.scad_buf);
```

### `src/main.cpp` (ImGui init)

```cpp
// ImGui 1.91.1+ uses PlatformIO for clipboard (old ImGuiIO fields are no-ops).
ImGui::GetPlatformIO().Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* { return ""; };
ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
    js_set_clipboard(text);
};
```

---

## References

- https://github.com/pthom/hello_imgui/issues/3 — identified the `window paste` +
  `stopImmediatePropagation` pattern
- `imgui_widgets.cpp` `InputTextEx()` — the `!is_ctrl_down` guard on character input;
  the `Shortcut()` routing system for copy/cut
- `imgui.cpp` `SetShortcutRouting()` — one-frame routing delay and `RoutingCurr` test
- ImGui changelog 1.91.1 — clipboard functions moved from `ImGuiIO` to `ImGuiPlatformIO`
- SDL2 Emscripten backend — calls `preventDefault()` on all canvas keydown events
