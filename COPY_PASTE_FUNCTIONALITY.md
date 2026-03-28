# Copy/Paste in the ImGui/Emscripten/SDL2 Browser App

## Paste (system → ImGui)

### Problem 1 — SDL eats the paste shortcut

SDL2's Emscripten backend registers a `keydown` listener on the canvas that calls
`e.preventDefault()` on every key event, which cancels the browser's paste action
so no `paste` event ever fires.

**Fix:** a capture-phase `window` keydown listener that calls
`e.stopImmediatePropagation()` for Ctrl+V and Shift+Insert. SDL's canvas listener
never runs, so the browser fires the `paste` event normally.

### Problem 2 — reading clipboard text

Inside a trusted `paste` event, `e.clipboardData.getData('text/plain')` is available
with no permission prompt. We store the result in `globalThis._pendingPaste` and
consume it from C++ each frame via `js_take_pending_paste()`.

`navigator.clipboard.readText()` was tried first — it triggers Chrome's interactive
clipboard permission UI even when the permission is already granted.

### Problem 3 — ImGui ignores characters while Ctrl is held

`AddInputCharactersUTF8` adds to `ImGuiIO::InputQueueCharacters`, but `InputTextEx`
silently discards this queue whenever any Ctrl modifier is down (it treats every
Ctrl+key as a potential shortcut). The paste event fires while Ctrl is still
physically held.

**Fix:** buffer the text in `AppState::paste_pending` and inject it in the first
frame where `!io.KeyCtrl`.

---

## Copy (ImGui → system)

### Problem 1 — SDL2 backend overwrites clipboard hooks

`ImGui_ImplSDL2_InitForOpenGL` sets `PlatformIO.Platform_SetClipboardTextFn` to
`SDL_SetClipboardText` and `Platform_GetClipboardTextFn` to `SDL_GetClipboardText`.
Both are no-ops in a browser. Any clipboard hooks must be set **after** calling
`ImGui_ImplSDL2_InitForOpenGL`.

### Problem 2 — ImGui's Shortcut() routing has a one-frame delay

ImGui's copy/cut handling uses `Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ...)` which
goes through a routing system that submits for the next frame and tests the current
frame's `RoutingCurr`. On the first press frame the route isn't yet confirmed, so
the shortcut fires one frame late. For Ctrl+X this is papered over by the
`ImGuiInputFlags_Repeat` flag (fires again on hold); for Ctrl+C (no repeat) the
key-pressed window closes before the route is confirmed.

Rather than trying to work around this, we use a poll-based approach.

### Solution — poll ImGui's clipboard each frame

`Platform_SetClipboardTextFn` stores the clipboard text in `AppState::imgui_clipboard`
(no system write). `Platform_GetClipboardTextFn` returns it, so ImGui-internal paste
(Ctrl+V via `GetClipboardText`) would work too if needed.

Each frame we compare `imgui_clipboard` against `imgui_clipboard_last`. When they
differ, we push the new content to the system clipboard via `navigator.clipboard.writeText()`
and update `imgui_clipboard_last`. This covers Ctrl+C, Ctrl+X, and any other action
that writes to ImGui's clipboard — with no custom keyboard interception on our side.

---

## Implementation summary

### `web/shell.html`

```js
window.addEventListener('paste', function(e) {
  var text = e.clipboardData && e.clipboardData.getData('text/plain');
  if (text) globalThis._pendingPaste = text;
  e.preventDefault();
});

window.addEventListener('keydown', function(e) {
  var ctrl = e.ctrlKey || e.metaKey, shift = e.shiftKey, k = e.key;

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

### `src/main.cpp` — init (after `ImGui_ImplSDL2_InitForOpenGL`)

```cpp
// Must be set AFTER ImGui_ImplSDL2_InitForOpenGL, which overwrites these with SDL stubs.
ImGui::GetPlatformIO().Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
    return g_app.imgui_clipboard.c_str();
};
ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
    g_app.imgui_clipboard = text;
};
```

### `src/main.cpp` — per-frame (after `ImGui::NewFrame()`)

```cpp
// Paste: buffer incoming text until Ctrl is released.
char* paste_text = js_take_pending_paste();
if (paste_text) { g_app.paste_pending = paste_text; free(paste_text); }
if (!g_app.paste_pending.empty() && !ImGui::GetIO().KeyCtrl) {
    ImGui::GetIO().AddInputCharactersUTF8(g_app.paste_pending.c_str());
    g_app.paste_pending.clear();
}

// Copy: push to system clipboard whenever ImGui's internal clipboard changes.
if (g_app.imgui_clipboard != g_app.imgui_clipboard_last) {
    js_set_clipboard(g_app.imgui_clipboard.c_str());
    g_app.imgui_clipboard_last = g_app.imgui_clipboard;
}
```

---

## References

- https://github.com/pthom/hello_imgui/issues/3 — `window paste` + `stopImmediatePropagation` pattern
- `imgui_widgets.cpp` `InputTextEx()` — `!is_ctrl_down` guard; `Shortcut()` routing for copy/cut
- `imgui_impl_sdl2.cpp` line ~531 — SDL2 backend overwrites `Platform_SetClipboardTextFn`
- ImGui changelog 1.91.1 — clipboard moved from `ImGuiIO` to `ImGuiPlatformIO`
