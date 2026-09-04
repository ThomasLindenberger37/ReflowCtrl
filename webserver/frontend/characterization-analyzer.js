"use strict";

/* Pure CSV parsing and characterization analysis; intentionally independent of the DOM. */
window.CharacterizationAnalyzer = (() => {
  const BIN_WIDTH_C = 25;
  const MIN_BIN_SAMPLES = 8;
  const MIN_COOLING_SAMPLE_COUNT = 10;
  const MIN_COOLING_TIME_SPAN_SECONDS = 2;
  const MIN_COOLING_DROP_C = 0.5;
  const MAX_COOLING_SEGMENT_GAP_MS = 1500;

  function median(values) {
    const sorted = [...values].sort((a, b) => a - b);
    const center = Math.floor(sorted.length / 2);
    return sorted.length % 2 ? sorted[center] : (sorted[center - 1] + sorted[center]) / 2;
  }

  function regression(samples) {
    const first = samples[0].time_ms;
    const values = samples.map(sample => ({ x: (sample.time_ms - first) / 1000, y: sample.temperature_c }));
    const meanX = values.reduce((sum, value) => sum + value.x, 0) / values.length;
    const meanY = values.reduce((sum, value) => sum + value.y, 0) / values.length;
    const divisor = values.reduce((sum, value) => sum + (value.x - meanX) ** 2, 0);
    if (divisor === 0) return null;
    return values.reduce((sum, value) => sum + (value.x - meanX) * (value.y - meanY), 0) / divisor;
  }

  function parse(text) {
    const rows = text.replace(/^\uFEFF/, "").trim().split(/\r?\n/).filter(Boolean);
    if (rows.length < 2) throw new Error("CSV needs a header and at least one sample.");
    const headers = rows[0].split(",").map(value => value.trim());
    const index = name => headers.indexOf(name);
    const timeIndex = index("time_ms");
    const temperatureIndex = index("temperature_filtered_c") >= 0 ? index("temperature_filtered_c") : index("temperature_c");
    const heaterIndex = index("heater_output");
    const phaseIndex = index("phase");
    if ([timeIndex, temperatureIndex, heaterIndex, phaseIndex].some(value => value < 0)) {
      throw new Error("CSV must contain time_ms, temperature_c (or temperature_filtered_c), heater_output, and phase.");
    }
    const invalidRows = [];
    const samples = [];
    for (let row = 1; row < rows.length; row++) {
      const columns = rows[row].split(",");
      const time_ms = Number(columns[timeIndex]);
      const temperature_c = Number(columns[temperatureIndex]);
      const heater_output = Number(columns[heaterIndex]);
      const phase = (columns[phaseIndex] || "").trim();
      if (!Number.isFinite(time_ms) || !Number.isFinite(temperature_c) || ![0, 1].includes(heater_output) || !phase) {
        invalidRows.push(row + 1); continue;
      }
      samples.push({ time_ms, temperature_c, heater_output, phase });
    }
    if (samples.length < 20) throw new Error("CSV has fewer than 20 valid samples.");
    return { samples, inputSamples: rows.length - 1, invalidRows };
  }

  function normalize(parsed) {
    const warnings = [];
    let duplicateSamples = 0;
    const timestampRegressionsInput = parsed.samples.slice(1).reduce((count, sample, index) => count + (sample.time_ms < parsed.samples[index].time_ms ? 1 : 0), 0);
    const unique = [];
    const seen = new Set();
    for (let index = 0; index < parsed.samples.length; index++) {
      const sample = parsed.samples[index];
      const key = `${sample.time_ms}|${sample.temperature_c}|${sample.heater_output}|${sample.phase}`;
      if (seen.has(key)) { duplicateSamples++; continue; }
      seen.add(key);
      unique.push(sample);
    }
    const timestampCounts = new Set(unique.map(sample => sample.time_ms));
    let samples = unique;
    if (timestampRegressionsInput) {
      if (timestampCounts.size === unique.length) samples = [...unique].sort((a, b) => a.time_ms - b.time_ms);
      else warnings.push("Timestamp regressions were retained because timestamps are ambiguous.");
    }
    const timestampRegressionsAfterCleanup = samples.slice(1).reduce((count, sample, index) => count + (sample.time_ms < samples[index].time_ms ? 1 : 0), 0);
    const gaps = samples.slice(1).map((sample, index) => sample.time_ms - samples[index].time_ms).filter(gap => gap >= 0);
    const expectedIntervalMs = gaps.length ? median(gaps) : 0;
    const largestGapMs = gaps.length ? Math.max(...gaps) : 0;
    if (parsed.invalidRows.length) warnings.push(`${parsed.invalidRows.length} invalid CSV rows ignored.`);
    if (duplicateSamples) warnings.push(`${duplicateSamples} identical duplicate samples removed.`);
    if (timestampRegressionsInput) warnings.push(`${timestampRegressionsInput} timestamp regressions detected in input.`);
    if (largestGapMs > Math.max(1000, expectedIntervalMs * 5)) warnings.push(`Large sampling gap: ${(largestGapMs / 1000).toFixed(1)} s.`);
    return { samples, quality: { inputSamples: parsed.inputSamples, usedSamples: samples.length, duplicateSamples, timestampRegressions: timestampRegressionsInput, timestampRegressionsInput, timestampRegressionsAfterCleanup, largestGapMs, expectedIntervalMs, warnings } };
  }

  function rates(samples, heaterOutput, requireFalling) {
    const bins = new Map();
    for (const sample of samples) {
      if (sample.heater_output !== heaterOutput) continue;
      const bin = Math.floor(sample.temperature_c / BIN_WIDTH_C) * BIN_WIDTH_C;
      if (!bins.has(bin)) bins.set(bin, []);
      bins.get(bin).push(sample);
    }
    return [...bins.entries()].map(([minimum, values]) => {
      const slope = values.length >= MIN_BIN_SAMPLES ? regression(values) : null;
      return { temperatureC: minimum + BIN_WIDTH_C / 2, rateCPerSecond: slope, sampleCount: values.length };
    }).filter(result => result.rateCPerSecond !== null && (!requireFalling || result.rateCPerSecond < 0));
  }

  function overshoots(samples) {
    const results = [];
    for (let index = 1; index < samples.length; index++) {
      if (samples[index - 1].heater_output !== 1 || samples[index].heater_output !== 0) continue;
      const start = samples[index];
      let peak = start;
      for (let next = index + 1; next < samples.length && samples[next].heater_output === 0; next++) {
        if (samples[next].temperature_c > peak.temperature_c) peak = samples[next];
      }
      results.push({ phase: start.phase, temperatureC: start.temperature_c, overshootC: peak.temperature_c - start.temperature_c, timeToPeakSeconds: (peak.time_ms - start.time_ms) / 1000 });
    }
    return results;
  }

  function cooldownSegments(samples) {
    const segments = [];
    let segment = [];
    for (const sample of samples) {
      const followsPrevious = segment.length && sample.time_ms - segment.at(-1).time_ms <= MAX_COOLING_SEGMENT_GAP_MS;
      if (sample.phase === "cooldown" && sample.heater_output === 0 && followsPrevious) {
        segment.push(sample);
      } else {
        if (segment.length) segments.push(segment);
        segment = sample.phase === "cooldown" && sample.heater_output === 0 ? [sample] : [];
      }
    }
    if (segment.length) segments.push(segment);
    return segments;
  }

  function coolingCandidate(samples) {
    if (samples.length < MIN_COOLING_SAMPLE_COUNT) return null;
    const timeSpanSeconds = (samples.at(-1).time_ms - samples[0].time_ms) / 1000;
    const temperatureDrop = samples[0].temperature_c - samples.at(-1).temperature_c;
    if (timeSpanSeconds < MIN_COOLING_TIME_SPAN_SECONDS || temperatureDrop < MIN_COOLING_DROP_C) return null;
    const rate = regression(samples);
    return rate !== null && rate < 0 ? { rate, sampleCount: samples.length, timeSpanSeconds } : null;
  }

  function passiveCoolingRates(samples) {
    const bestByBin = new Map();
    for (const segment of cooldownSegments(samples)) {
      let peakIndex = 0;
      for (let index = 1; index < segment.length; index++) {
        if (segment[index].temperature_c > segment[peakIndex].temperature_c) peakIndex = index;
      }
      const falling = segment.slice(peakIndex);
      let binSamples = [];
      let currentBin = null;
      const submit = () => {
        if (currentBin === null) return;
        const candidate = coolingCandidate(binSamples);
        if (!candidate || (bestByBin.get(currentBin)?.timeSpanSeconds ?? 0) >= candidate.timeSpanSeconds) return;
        bestByBin.set(currentBin, candidate);
      };
      for (const sample of falling) {
        const bin = Math.floor(sample.temperature_c / BIN_WIDTH_C) * BIN_WIDTH_C;
        if (currentBin !== null && bin !== currentBin) { submit(); binSamples = []; }
        currentBin = bin; binSamples.push(sample);
      }
      submit();
    }
    return [...bestByBin.entries()].sort(([first], [second]) => first - second).map(([minimum, candidate]) => ({ temperatureC: minimum + BIN_WIDTH_C / 2, rateCPerSecond: candidate.rate, sampleCount: candidate.sampleCount }));
  }

  function analyze(text) {
    const normalized = normalize(parse(text));
    const { samples, quality } = normalized;
    const baseline = samples.filter(sample => sample.phase === "baseline").map(sample => sample.temperature_c);
    if (!baseline.length) quality.warnings.push("No baseline samples found; ambient temperature uses the first sample.");
    const heatingRate = rates(samples, 1, false);
    const passiveCoolingRate = passiveCoolingRates(samples);
    const coastOvershoot = overshoots(samples);
    if (!heatingRate.length) quality.warnings.push("No temperature bin has enough heater-on samples.");
    if (!passiveCoolingRate.length) quality.warnings.push("No stable passive cooling bin has enough falling samples.");
    const maximumHeatingRate = heatingRate.length ? Math.max(...heatingRate.map(rate => rate.rateCPerSecond)) : null;
    const maximumObservedOvershootC = coastOvershoot.length ? Math.max(...coastOvershoot.map(item => item.overshootC)) : null;
    const result = {
      formatVersion: 1, type: "oven-characterization",
      ambientTemperatureC: Number((baseline.length ? median(baseline) : samples[0].temperature_c).toFixed(2)),
      heatingRate: heatingRate.map(rate => ({ ...rate, rateCPerSecond: Number(rate.rateCPerSecond.toFixed(4)) })),
      passiveCoolingRate: passiveCoolingRate.map(rate => ({ ...rate, rateCPerSecond: Number(rate.rateCPerSecond.toFixed(4)) })),
      coastOvershoot: coastOvershoot.map(item => ({ ...item, overshootC: Number(item.overshootC.toFixed(2)), timeToPeakSeconds: Number(item.timeToPeakSeconds.toFixed(2)) })),
      estimatedEquilibriumTemperatureC: null,
      summary: {
        minimumTemperatureC: Math.min(...samples.map(sample => sample.temperature_c)), maximumTemperatureC: Math.max(...samples.map(sample => sample.temperature_c)),
        maximumHeatingRateCPerSecond: maximumHeatingRate === null ? null : Number(maximumHeatingRate.toFixed(4)),
        maximumObservedOvershootC: maximumObservedOvershootC === null ? null : Number(maximumObservedOvershootC.toFixed(2)),
        totalDurationSeconds: Number(((samples.at(-1).time_ms - samples[0].time_ms) / 1000).toFixed(2))
      }, dataQuality: quality
    };
    return { result, samples };
  }

  return { analyze };
})();
