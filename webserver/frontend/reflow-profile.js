(function exposeReflowProfile(root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.ReflowProfile = api;
}(typeof globalThis !== "undefined" ? globalThis : this, function createReflowProfile() {
  "use strict";

  const AMBIENT_TEMPERATURE_C = 25;
  const COOLING_RATE_C_PER_S = 2.5;

  function numberAt(config, path) {
    return path.split(".").reduce((value, key) => value?.[key], config);
  }

  function validateProfile(config) {
    const errors = {};
    const setError = (field, message) => { if (!errors[field]) errors[field] = message; };
    const requirePositive = (field, label) => {
      const value = Number(numberAt(config, field));
      if (!Number.isFinite(value) || value <= 0) setError(field, `${label} must be greater than 0.`);
      return value;
    };

    const name = String(config?.name ?? "").trim();
    if (!name) setError("name", "Enter a profile name.");
    else if (name.length > 64) setError("name", "Use no more than 64 characters.");

    const rampRate = requirePositive("max_ramp_rate_c_per_s", "Maximum heating rate");
    const soakStart = requirePositive("soak.start_temperature_c", "Soak start temperature");
    const soakEnd = requirePositive("soak.end_temperature_c", "Soak end temperature");
    const soakDuration = requirePositive("soak.duration_s", "Soak duration");
    const liquidus = requirePositive("reflow.liquidus_temperature_c", "Liquidus temperature");
    const peak = requirePositive("reflow.peak_temperature_c", "Peak temperature");
    const timeAbove = requirePositive("reflow.time_above_liquidus_s", "Time above liquidus");

    if (Number.isFinite(soakStart) && soakStart <= AMBIENT_TEMPERATURE_C) {
      setError("soak.start_temperature_c", `Must be above ${AMBIENT_TEMPERATURE_C} °C.`);
    }
    if (Number.isFinite(soakStart) && Number.isFinite(soakEnd) && soakEnd <= soakStart) {
      setError("soak.end_temperature_c", "Must be higher than the soak start temperature.");
    }
    if (Number.isFinite(soakEnd) && Number.isFinite(liquidus) && liquidus <= soakEnd) {
      setError("reflow.liquidus_temperature_c", "Must be higher than the soak end temperature.");
    }
    if (Number.isFinite(liquidus) && Number.isFinite(peak) && peak <= liquidus) {
      setError("reflow.peak_temperature_c", "Must be higher than the liquidus temperature.");
    }

    if ([soakStart, soakEnd, soakDuration, rampRate].every(Number.isFinite) && soakDuration > 0 && rampRate > 0) {
      const requiredSoakRate = (soakEnd - soakStart) / soakDuration;
      if (requiredSoakRate > rampRate) {
        setError("soak.duration_s", `Increase duration to at least ${Math.ceil((soakEnd - soakStart) / rampRate)} s to respect the ramp limit.`);
      }
    }

    if ([liquidus, peak, timeAbove, rampRate].every(Number.isFinite) && rampRate > 0 && peak > liquidus) {
      const minimumAscentTime = (peak - liquidus) / rampRate;
      if (timeAbove <= minimumAscentTime) {
        setError("reflow.time_above_liquidus_s", `Must be greater than ${minimumAscentTime.toFixed(1)} s for this peak and ramp rate.`);
      }
    }

    return { valid: Object.keys(errors).length === 0, errors };
  }

  function getProfileMetadata(config) {
    const rate = Number(config.max_ramp_rate_c_per_s);
    const soakStart = Number(config.soak.start_temperature_c);
    const soakEnd = Number(config.soak.end_temperature_c);
    const soakDuration = Number(config.soak.duration_s);
    const liquidus = Number(config.reflow.liquidus_temperature_c);
    const peak = Number(config.reflow.peak_temperature_c);
    const timeAbove = Number(config.reflow.time_above_liquidus_s);

    const rampToSoakEnd = (soakStart - AMBIENT_TEMPERATURE_C) / rate;
    const soakEndTime = rampToSoakEnd + soakDuration;
    const liquidusUpTime = soakEndTime + (liquidus - soakEnd) / rate;
    const minimumPeakAscent = (peak - liquidus) / rate;
    const aboveLiquidusAscent = Math.max(minimumPeakAscent, timeAbove / 2);
    const peakTime = liquidusUpTime + aboveLiquidusAscent;
    const liquidusDownTime = liquidusUpTime + timeAbove;
    const endTime = liquidusDownTime + (liquidus - AMBIENT_TEMPERATURE_C) / COOLING_RATE_C_PER_S;

    return {
      ambientTemperatureC: AMBIENT_TEMPERATURE_C,
      coolingRateCPerS: COOLING_RATE_C_PER_S,
      rampToSoakEnd,
      soakEndTime,
      liquidusUpTime,
      peakTime,
      liquidusDownTime,
      endTime,
      segments: [
        { phase: "ramp", start: 0, end: rampToSoakEnd, from: AMBIENT_TEMPERATURE_C, to: soakStart },
        { phase: "soak", start: rampToSoakEnd, end: soakEndTime, from: soakStart, to: soakEnd },
        { phase: "ramp", start: soakEndTime, end: liquidusUpTime, from: soakEnd, to: liquidus },
        { phase: "reflow", start: liquidusUpTime, end: peakTime, from: liquidus, to: peak },
        { phase: "reflow", start: peakTime, end: liquidusDownTime, from: peak, to: liquidus },
        { phase: "cooling", start: liquidusDownTime, end: endTime, from: liquidus, to: AMBIENT_TEMPERATURE_C }
      ]
    };
  }

  function generateProfile(config) {
    const validation = validateProfile(config);
    if (!validation.valid) return [];

    const metadata = getProfileMetadata(config);
    const sampleTimes = new Set();
    for (let time = 0; time <= Math.floor(metadata.endTime); time += 1) sampleTimes.add(time);
    metadata.segments.forEach(segment => {
      sampleTimes.add(segment.start);
      sampleTimes.add(segment.end);
    });

    return [...sampleTimes]
      .sort((left, right) => left - right)
      .map(time => {
        const segment = metadata.segments.find(item => time <= item.end + 1e-9) ?? metadata.segments.at(-1);
        const duration = segment.end - segment.start;
        const progress = duration > 0 ? Math.min(1, Math.max(0, (time - segment.start) / duration)) : 1;
        const temperature = segment.from + ((segment.to - segment.from) * progress);
        return {
          time_s: Number(time.toFixed(3)),
          temperature_c: Number(temperature.toFixed(2))
        };
      });
  }

  return Object.freeze({
    AMBIENT_TEMPERATURE_C,
    validateProfile,
    generateProfile,
    getProfileMetadata
  });
}));
