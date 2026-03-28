// @ts-check
const { test, expect } = require('@playwright/test');

// ---------------------------------------------------------------------------
// Clipboard paste mechanism tests
//
// The app uses a hidden <textarea> to capture paste events without triggering
// Chrome's clipboard permission popup (navigator.clipboard.readText() would
// trigger it; the textarea approach does not).
//
// Mechanism (shell.html):
//   1. Ctrl+V keydown on #canvas  → focus moved to the hidden <textarea>
//   2. Browser fires 'paste' on textarea → listener stores text in
//      globalThis._pendingPaste and restores #canvas focus
//   3. C++ calls js_take_pending_paste() every frame → clears _pendingPaste
//
// Note on headless Chromium and Ctrl+V:
//   page.keyboard.press('Control+v') injects a synthetic keydown but does NOT
//   cause the browser to fire a real 'paste' event from the OS clipboard in
//   headless mode.  The reliable path is to dispatch a ClipboardEvent directly
//   on the textarea, which exercises exactly the same event handler.
//   The keyboard test is skipped in CI but kept for local --headed runs.
// ---------------------------------------------------------------------------

test.beforeEach(async ({ page }) => {
  await page.goto('/');
  await page.waitForFunction(
    () => typeof globalThis._openscadFactory === 'function',
    { timeout: 30_000 },
  );
});

// Primary regression test — reliable in headless CI.
test('dispatching paste event on hidden textarea sets _pendingPaste', async ({ page }) => {
  await page.evaluate(() => {
    const dt = new DataTransfer();
    dt.setData('text/plain', 'sphere(5);');
    const ev = new ClipboardEvent('paste', { clipboardData: dt, bubbles: true });
    // _pasteTA is declared as `var` in shell.html → accessible as window._pasteTA
    window._pasteTA.dispatchEvent(ev);
  });

  const pending = await page.evaluate(() => globalThis._pendingPaste);
  expect(pending).toBe('sphere(5);');
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
  // Register a non-capture listener that records keys.
  await page.evaluate(() => {
    globalThis._seenKeys = [];
    window.addEventListener('keydown', (e) => globalThis._seenKeys.push(e.key));
  });

  await page.locator('#canvas').focus();
  await page.keyboard.press('a');

  const seen = await page.evaluate(() => globalThis._seenKeys);
  expect(seen).toContain('a');
});

// Integration test — requires a headed browser and real OS clipboard.
// Run locally with:  npx playwright test --headed tests/paste.spec.js
test.skip('Ctrl+V on canvas routes text through hidden textarea (headed only)', async ({ page }) => {
  await page.evaluate(() => navigator.clipboard.writeText('cube([5, 5, 5]);'));
  await page.locator('#canvas').click();
  await page.keyboard.press('Control+v');

  await page.waitForFunction(
    () => globalThis._pendingPaste === 'cube([5, 5, 5]);',
    { timeout: 3_000 },
  );
});
