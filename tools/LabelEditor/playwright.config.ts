import { defineConfig } from "@playwright/test";

/**
 * The editor's browser tests drive the Chrome already on the machine (`channel: chrome`),
 * so no browser download is needed. `npm run dev` is started automatically unless a dev
 * server is already on 5178.
 */
export default defineConfig({
  testDir: "./e2e",
  fullyParallel: false,
  forbidOnly: !!process.env.CI,
  retries: 0,
  reporter: [["list"]],
  use: {
    baseURL: "http://localhost:5178",
    channel: "chrome",
    viewport: { width: 1440, height: 900 },
    trace: "retain-on-failure",
  },
  webServer: {
    command: "npm run dev",
    url: "http://localhost:5178",
    reuseExistingServer: true,
    timeout: 60_000,
  },
  projects: [
    {
      name: "chrome",
      testIgnore: /webmcp-native\.spec\.ts/,
      use: { channel: "chrome" },
    },
    {
      // Chrome's own WebMCP behind the feature flag; the spec skips when the API is absent.
      name: "webmcp-native",
      testMatch: /webmcp-native\.spec\.ts/,
      use: {
        channel: "chrome",
        launchOptions: { args: ["--enable-features=WebMCP"] },
      },
    },
  ],
});
