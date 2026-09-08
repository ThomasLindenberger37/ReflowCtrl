const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");

const source = fs.readFileSync(path.join(__dirname, "../webserver/frontend/app.js"), "utf8");
const configuration = {
  formatVersion: 1, type: "oven-characterization", ambientTemperatureC: 25,
  heatingRate: [{ temperatureC: 80, rateCPerSecond: 0.9, sampleCount: 30 }],
  passiveCoolingRate: [], coastOvershoot: [], estimatedEquilibriumTemperatureC: null,
  summary: { minimumTemperatureC: 25, maximumTemperatureC: 240,
    maximumHeatingRateCPerSecond: 0.9, maximumObservedOvershootC: null, totalDurationSeconds: 800 },
  dataQuality: { inputSamples: 4000, usedSamples: 4000, warnings: [] }
};

function browser(apiRequest) {
  const canvas = new Proxy({}, { get: () => () => {} });
  const document = {
    querySelector: () => ({ textContent: "", disabled: true, width: 360, height: 180,
      getContext: () => canvas, replaceChildren() {} }),
    createElement: () => ({})
  };
  const context = vm.createContext({ document, apiRequest, TextEncoder });
  vm.runInContext(source.slice(source.indexOf("const characterization ="), source.indexOf("async function pollStatus()")), context);
  return {
    context,
    run: code => vm.runInContext(code, context),
    import: async (value = configuration) => {
      context.event = { target: { files: [{ name: "oven.configuration.json", text: async () => JSON.stringify(value) }], value: "file" } };
      await vm.runInContext("loadCharacterizationFile(event)", context);
    }
  };
}

test("import, save, and restore in a new browser session", async () => {
  let stored = null;
  const api = async (url, options = {}) => {
    assert.equal(url, "/characterization/configuration");
    if (options.method === "PUT") { stored = JSON.parse(options.body); return { saved: true }; }
    return stored;
  };
  const first = browser(api);
  await first.import();
  assert.equal(first.run("characterization.saveConfig.disabled"), false);
  await first.run("saveCharacterizationConfiguration()");
  assert.deepEqual(stored, configuration);
  const second = browser(api);
  await second.run("restoreCharacterizationConfiguration()");
  assert.equal(second.run("characterization.analysis.ambientTemperatureC"), 25);
  assert.match(second.run("characterization.storageMessage.textContent"), /Loaded saved/);
});

test("storage errors are visible and saving can be retried", async () => {
  const page = browser(async () => { throw new Error("Flash full"); });
  await page.import();
  await page.run("saveCharacterizationConfiguration()");
  assert.match(page.run("characterization.storageMessage.textContent"), /Could not save.*Flash full/);
  assert.equal(page.run("characterization.saveConfig.disabled"), false);
});

test("invalid import preserves the current analysis", async () => {
  const page = browser(async () => null);
  await page.import();
  await page.import({ ...configuration, heatingRate: [{}] });
  assert.equal(page.run("characterization.analysis.heatingRate[0].temperatureC"), 80);
  assert.match(page.run("characterization.storageMessage.textContent"), /Could not load file/);
});

test("late restore does not overwrite a newly imported configuration", async () => {
  let resolve;
  const page = browser(() => new Promise(done => { resolve = done; }));
  const restoring = page.run("restoreCharacterizationConfiguration()");
  await page.import();
  resolve({ ...configuration, ambientTemperatureC: 15 });
  await restoring;
  assert.equal(page.run("characterization.analysis.ambientTemperatureC"), 25);
});
