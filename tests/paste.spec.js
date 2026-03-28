// @ts-check
const { test, expect } = require('@playwright/test');

// ---------------------------------------------------------------------------
// Clipboard paste mechanism tests
//
// Mechanism (shell.html):
//   1. Ctrl+V (or Shift+Insert) keydown on #canvas
//      → capture-phase listener calls stopImmediatePropagation()
//      → SDL never sees the event, never calls preventDefault()
//      → browser executes its default paste action
//   2. Browser fires 'paste' event; our window listener reads e.clipboardData
//      → sets globalThis._pendingPaste (no DOM focus change, no permission prompt)
//   3. C++ calls js_take_pending_paste() every frame → clears _pendingPaste
//      and injects text via ImGui::GetIO().AddInputCharactersUTF8()
//
// No focus change is made, so ImGui retains its active widget and the text
// lands in whichever InputText currently has keyboard focus.
// ---------------------------------------------------------------------------

test.beforeEach(async ({ page }) => {
  await page.goto('/');
  await page.waitForFunction(
    () => typeof globalThis._openscadFactory === 'function',
    { timeout: 30_000 },
  );
});


// Verifies C++ consumes _pendingPaste within a frame or two.
test('_pendingPaste is consumed by the C++ render loop', async ({ page }) => {
  await page.evaluate(() => {
    globalThis._pendingPaste = 'cylinder(r=5, h=10);';
  });

  // ImGui runs at ~60 fps; this should clear within milliseconds.
  await page.waitForFunction(() => !globalThis._pendingPaste, { timeout: 5_000 });
});

// Verifies the capture listener does not block ordinary keys from propagating.
test('non-shortcut keydown events propagate past the capture listener', async ({ page }) => {
  await page.evaluate(() => {
    globalThis._seenKeys = [];
    window.addEventListener('keydown', (e) => globalThis._seenKeys.push(e.key));
  });

  await page.locator('#canvas').focus();
  await page.keyboard.press('a');

  const seen = await page.evaluate(() => globalThis._seenKeys);
  expect(seen).toContain('a');
});
