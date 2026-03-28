# Copy/Paste in the ImGui/Emscripten/SDL2 Browser App

## The Problem (three layers)

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
first subsequent frame where `!io.KeyCtrl`. This causes the small but acceptable
delay after releasing Ctrl.

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
// Buffer incoming paste (arrives while Ctrl is still held).
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
```

### `src/main.cpp` (ImGui init)

```cpp
// SDL2 clipboard API doesn't work in browsers; wire up JS clipboard instead.
io.GetClipboardTextFn = [](void*) -> const char* { return ""; };
io.SetClipboardTextFn = [](void*, const char* text) { js_set_clipboard(text); };
```

`js_set_clipboard` calls `navigator.clipboard.writeText()` (async, best-effort).

---

## References

- https://github.com/pthom/hello_imgui/issues/3 — the thread that identified the
  `window paste` + `stopImmediatePropagation` pattern
- `imgui_widgets.cpp` `InputTextEx()` — the `!is_ctrl_down` guard on character input
- SDL2 Emscripten backend — calls `preventDefault()` on all canvas keydown events
