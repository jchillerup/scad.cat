// @ts-check
const { test, expect } = require('@playwright/test');

// ---------------------------------------------------------------------------
// OpenSCAD evaluation pipeline tests
//
// These tests call _openscadFactory directly from page.evaluate() rather than
// through the C++ render loop.  That isolates the JS evaluation layer and
// avoids depending on ImGui frame timing.
//
// The async IIFE pattern is used so that page.evaluate() returns immediately
// while the (slow) OpenSCAD WASM compilation runs in the background.  A
// subsequent waitForFunction() polls until the result lands in a JS global.
// ---------------------------------------------------------------------------

test.beforeEach(async ({ page }) => {
  await page.goto('/');
  await page.waitForFunction(
    () => typeof globalThis._openscadFactory === 'function',
    { timeout: 30_000 },
  );
});

test('valid SCAD compiles to STL bytes', async ({ page }) => {
  // Kick off compilation without blocking page.evaluate().
  await page.evaluate(() => {
    globalThis._testEvalStatus = 'running';
    globalThis._testStlBytes = null;
    (async () => {
      try {
        const os = await globalThis._openscadFactory({ noInitialRun: true });
        os.FS.writeFile('/model.scad', 'cube([10, 10, 10]);');
        const code = os.callMain([
          '/model.scad',
          '--enable=manifold',
          '--export-format', 'binstl',
          '-o', '/out.stl',
        ]);
        if (code === 0) {
          globalThis._testStlBytes = os.FS.readFile('/out.stl', { encoding: 'binary' });
          globalThis._testEvalStatus = 'done';
        } else {
          globalThis._testEvalStatus = 'error';
        }
      } catch (e) {
        console.error('[test scad_eval]', e);
        globalThis._testEvalStatus = 'error';
      }
    })();
  });

  await page.waitForFunction(
    () => globalThis._testEvalStatus !== 'running',
    { timeout: 60_000 },
  );

  const status = await page.evaluate(() => globalThis._testEvalStatus);
  expect(status).toBe('done');

  // Binary STL: 80-byte header + 4-byte triangle count + N * 50 bytes.
  // A cube has 12 triangles → minimum 684 bytes.
  const byteLen = await page.evaluate(() =>
    globalThis._testStlBytes ? globalThis._testStlBytes.length : 0,
  );
  expect(byteLen).toBeGreaterThan(84);
});

test('invalid SCAD sets status to error', async ({ page }) => {
  await page.evaluate(() => {
    globalThis._testEvalStatus = 'running';
    (async () => {
      try {
        const os = await globalThis._openscadFactory({ noInitialRun: true });
        os.FS.writeFile('/model.scad', 'this is not valid openscad !!!');
        const code = os.callMain([
          '/model.scad',
          '--enable=manifold',
          '--export-format', 'binstl',
          '-o', '/out.stl',
        ]);
        globalThis._testEvalStatus = code === 0 ? 'done' : 'error';
      } catch (e) {
        globalThis._testEvalStatus = 'error';
      }
    })();
  });

  await page.waitForFunction(
    () => globalThis._testEvalStatus !== 'running',
    { timeout: 60_000 },
  );

  const status = await page.evaluate(() => globalThis._testEvalStatus);
  expect(status).toBe('error');
});
