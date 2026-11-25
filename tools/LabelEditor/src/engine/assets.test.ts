import { afterEach, describe, expect, it, vi } from "vitest";

import { faceUrl, publicAsset } from "./assets";

describe("publicAsset", () => {
  afterEach(() => vi.unstubAllEnvs());

  it("resolves a file under the development root", () => {
    vi.stubEnv("BASE_URL", "/");
    expect(publicAsset("fonts/archivo-narrow-600.ttf")).toBe(
      "/fonts/archivo-narrow-600.ttf",
    );
  });

  it("resolves a file under the Site's /k7art base, not the page's URL", () => {
    // The bug this guards: at `/k7art` without its slash, `./fonts/a.ttf` became
    // `/fonts/a.ttf` - the Site's page - and the three faces silently fell back.
    vi.stubEnv("BASE_URL", "/k7art/");
    expect(publicAsset("fonts/archivo-narrow-600.ttf")).toBe(
      "/k7art/fonts/archivo-narrow-600.ttf",
    );
    expect(publicAsset("/fonts/archivo-narrow-600.ttf")).toBe(
      "/k7art/fonts/archivo-narrow-600.ttf",
    );
  });
});

describe("faceUrl", () => {
  afterEach(() => vi.unstubAllEnvs());

  it("names a face under the build's base, never the document's directory", () => {
    vi.stubEnv("BASE_URL", "/k7art/");
    expect(faceUrl("barlow-condensed-700.ttf")).toBe(
      "/k7art/fonts/barlow-condensed-700.ttf",
    );
  });
});
