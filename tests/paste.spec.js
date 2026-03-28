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
//   1. Ctrl+V keydown on #canvas
//      → stopImmediatePropagation() (prevents SDL from calling preventDefault)
//      → focus moved to the hidden <textarea>
//   2. Browser fires 'paste' on textarea (it is now the focused element and
//      the paste action was not cancelled) → listener stores text in
//      globalThis._pendingPaste and restores #canvas focus
//   3. C++ calls js_take_pending_paste() every frame → clears _pendingPaste
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

// Ctrl+V fires the paste event on the hidden textarea.
// In headless Chromium, e.clipboardData is always empty (even if the clipboard
// was seeded with navigator.clipboard.writeText), so we can't assert on
// _pendingPaste here.  Instead we verify that the paste event itself fires on
// _pasteTA, which confirms:
//   1. Our capture handler ran (stopImmediatePropagation + _pasteTA.focus())
//   2. SDL did NOT call preventDefault() on the keydown (it never saw the event)
//   3. The browser executed the paste action on the now-focused textarea
test('Ctrl+V on canvas fires the paste event on the hidden textarea', async ({ page }) => {
  // Register a one-shot listener before we press the key.
  await page.evaluate(() => {
    globalThis._pasteFiredOnTA = false;
    window._pasteTA.addEventListener(
      'paste',
      () => { globalThis._pasteFiredOnTA = true; },
      { once: true },
    );
  });

  await page.locator('#canvas').click();
  await page.keyboard.press('Control+v');

  const fired = await page.evaluate(() => globalThis._pasteFiredOnTA);
  expect(fired).toBe(true);
});

// Full end-to-end: Ctrl+V fires the paste event and sets _pendingPaste.
// Skipped in headless CI because synthetic keyboard events don't trigger
// the browser's native paste action.
// Run locally with:  npx playwright test --headed tests/paste.spec.js
test.skip('Ctrl+V on canvas sets _pendingPaste via hidden textarea (headed only)', async ({ page }) => {
  await page.evaluate(() => navigator.clipboard.writeText('cube([5, 5, 5]);'));
  await page.locator('#canvas').click();
  await page.keyboard.press('Control+v');

  await page.waitForFunction(
    () => globalThis._pendingPaste === 'cube([5, 5, 5]);',
    { timeout: 3_000 },
  );
});
