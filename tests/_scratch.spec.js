// @ts-check
const { test, expect } = require('@playwright/test');

test('test', async ({ page }) => {
  await page.goto('/');
  await page.waitForFunction(
    () => typeof globalThis._openscadFactory === 'function',
    { timeout: 30_000 },
  );

  await page.locator('#canvas').click({
    position: {
      x: 292,
      y: 291
    }
  });

  await page.locator('#canvas').press('H');
  await page.locator('#canvas').press('e');
  await page.locator('#canvas').press('l');
  await page.locator('#canvas').press('l');
  await page.locator('#canvas').press('o');

  // Wait for _scadBuf to reflect the keypresses above
  await page.waitForFunction(
    () => typeof globalThis._scadBuf === 'string',
    { timeout: 5_000 },
  );
  const bufBefore = await page.evaluate(() => globalThis._scadBuf);

  // Paste via the window paste event (matches current shell.html implementation)
  const pasteText = 'sphere(5);';
  await page.evaluate((text) => {
    const dt = new DataTransfer();
    dt.setData('text/plain', text);
    window.dispatchEvent(
      new ClipboardEvent('paste', { clipboardData: dt, bubbles: true })
    );
  }, pasteText);

  // Wait for C++ to consume _pendingPaste and for _scadBuf to update
  await page.waitForFunction(
    (text) => !globalThis._pendingPaste && globalThis._scadBuf.includes(text),
    pasteText,
    { timeout: 5_000 },
  );

  const bufAfter = await page.evaluate(() => globalThis._scadBuf);
  expect(bufAfter).toContain(pasteText);
  expect(bufAfter.length).toBeGreaterThan(bufBefore.length);

  await page.pause();
});
