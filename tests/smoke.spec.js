// @ts-check
const { test, expect } = require('@playwright/test');

// ---------------------------------------------------------------------------
// Smoke tests — basic load and initialisation
// ---------------------------------------------------------------------------

test('canvas is visible', async ({ page }) => {
  await page.goto('/');
  await expect(page.locator('#canvas')).toBeVisible();
});

test('_openscadFactory is set by the ES module', async ({ page }) => {
  await page.goto('/');
  // The <script type="module"> in shell.html sets this once the import resolves.
  await page.waitForFunction(
    () => typeof globalThis._openscadFactory === 'function',
    { timeout: 30_000 },
  );
});

test('no uncaught JS errors on load', async ({ page }) => {
  const errors = [];
  page.on('pageerror', (err) => errors.push(err.message));

  await page.goto('/');
  await page.waitForFunction(
    () => typeof globalThis._openscadFactory === 'function',
    { timeout: 30_000 },
  );

  expect(errors).toHaveLength(0);
});
