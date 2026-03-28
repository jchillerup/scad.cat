// @ts-check
const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
  testDir: './tests',

  // Per-test wall-clock limit. openscad-wasm can take 30 s to compile on CI,
  // and the factory itself loads in a few seconds — be generous.
  timeout: 120_000,

  // Governs expect() / waitForFunction() default timeout.
  expect: { timeout: 90_000 },

  reporter: [['list'], ['html', { open: 'never' }]],

  use: {
    baseURL: 'http://localhost:8080',
    // Clipboard permission is needed for the copy round-trip test.
    permissions: ['clipboard-read', 'clipboard-write'],
    // Pass --no-sandbox in CI (GitHub Actions Ubuntu containers need it).
    launchOptions: process.env.CI
      ? { args: ['--no-sandbox', '--disable-setuid-sandbox'] }
      : {},
  },

  projects: [
    // Only Chromium — this app targets a specific deployment environment
    // and the paste mechanism has browser-specific behaviour.
    { name: 'chromium', use: { browserName: 'chromium' } },
  ],

  webServer: {
    command: 'python3 -m http.server 8080 --directory build/wasm',
    url: 'http://localhost:8080',
    // In local dev you can leave `mise run wasm` running and Playwright will
    // reuse it.  In CI a fresh server is started per run.
    reuseExistingServer: !process.env.CI,
    timeout: 10_000,
  },
});
