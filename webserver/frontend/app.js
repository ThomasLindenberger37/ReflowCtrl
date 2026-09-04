"use strict";

const API = "/api";
const MAX_CHART_POINTS = 300;
const SAFE_START_TEMPERATURE = 40;
const CONTROLS_AVAILABLE = false;
const stateLabels = { idle: "Idle", preheat: "Preheat", soak: "Soak", reflow: "Reflow", cooling: "Cooling", complete: "Complete", error: "Error" };

const ui = {
  temperature: document.querySelector("#temperature"), target: document.querySelector("#targetTemperature"),
  temperatureBar: document.querySelector("#temperatureBar"), heaterStatus: document.querySelector("#heaterStatus"),
  heaterIcon: document.querySelector("#heaterIcon"), heaterHint: document.querySelector("#heaterHint"),
  elapsed: document.querySelector("#elapsedTime"), stateBadge: document.querySelector("#stateBadge"),
  start: document.querySelector("#startButton"), stop: document.querySelector("#stopButton"),
  connectionDot: document.querySelector("#connectionDot"), connectionText: document.querySelector("#connectionText"),
  apiStatus: document.querySelector("#apiStatus"), processTitle: document.querySelector("#processTitle"),
  processNote: document.querySelector("#processNote"), toast: document.querySelector("#toast")
};

let chart;
let lastElapsed = null;
let toastTimer;
let debugCursor = 0;
let debugLineCount = 0;
let lastControllerStatus = null;
let profilePreviewChart;
let activeProfileName = "";
let editedProfileName = "";
let activeProfileHighlight = null;
const availableProfiles = new Map();

async function apiRequest(path, options = {}) {
  const response = await fetch(`${API}${path}`, { headers: { "Content-Type": "application/json" }, ...options });
  if (!response.ok) {
    const body = await response.json().catch(() => ({}));
    throw new Error(body.detail || `HTTP ${response.status}`);
  }
  return response.json();
}

function formatTime(seconds) {
  const minutes = Math.floor(seconds / 60).toString().padStart(2, "0");
  const rest = (seconds % 60).toString().padStart(2, "0");
  return `${minutes}:${rest}`;
}

function statusIsValid(status) {
  return status !== null && typeof status === "object" &&
    Object.hasOwn(stateLabels, status.state) &&
    Number.isFinite(Number(status.temperature)) &&
    Number.isFinite(Number(status.target_temperature)) &&
    Number.isInteger(status.elapsed_seconds) && status.elapsed_seconds >= 0 &&
    typeof status.heater === "boolean";
}

function updateStatus(status) {
  lastControllerStatus = status;
  const state = stateLabels[status.state] ? status.state : "error";
  ui.temperature.textContent = Number(status.temperature).toFixed(1);
  ui.target.textContent = Number(status.target_temperature).toFixed(1);
  ui.temperatureBar.style.width = `${Math.min(100, Math.max(0, status.temperature / 250 * 100))}%`;
  ui.heaterStatus.textContent = status.heater ? "ON" : "OFF";
  ui.heaterIcon.classList.toggle("on", status.heater);
  ui.heaterHint.textContent = status.heater ? "Power output active" : "Output disabled";
  ui.elapsed.textContent = formatTime(status.elapsed_seconds);
  ui.stateBadge.dataset.state = state;
  ui.stateBadge.querySelector("b").textContent = stateLabels[state].toUpperCase();

  const active = ["preheat", "soak", "reflow", "cooling"].includes(state);
  const temperatureLocked = status.temperature > SAFE_START_TEMPERATURE;
  ui.start.disabled = !CONTROLS_AVAILABLE || active || temperatureLocked;
  ui.start.classList.toggle("temperature-locked", temperatureLocked && !active);
  ui.start.title = temperatureLocked ? `Oven must cool to ${SAFE_START_TEMPERATURE.toFixed(1)} °C before starting` : "";
  ui.start.setAttribute("aria-label", temperatureLocked ? `Start disabled. Oven temperature is ${Number(status.temperature).toFixed(1)} degrees Celsius.` : "Start reflow process");
  ui.stop.disabled = !CONTROLS_AVAILABLE || !active;
  profileUi.select.disabled = !CONTROLS_AVAILABLE || active || availableProfiles.size === 0;
  profileUi.edit.disabled = !CONTROLS_AVAILABLE || active || !profileUi.select.value;
  ui.processTitle.textContent = active ? `${stateLabels[state]} in progress` : temperatureLocked ? "Oven cooling down" : state === "complete" ? "Profile complete" : "Ready for a new process";
  ui.processNote.textContent = active ? `Process time ${formatTime(status.elapsed_seconds)} · Automatic temperature control active` : temperatureLocked ? `Start is locked until the oven reaches ${SAFE_START_TEMPERATURE.toFixed(1)} °C or below.` : state === "complete" ? "The oven has cooled to a safe starting temperature." : "Start the profile to begin the reflow process.";

  if (lastElapsed !== status.elapsed_seconds) {
    addChartPoint(status);
    lastElapsed = status.elapsed_seconds;
  }
}

function setConnection(online) {
  ui.connectionDot.className = `connection-dot ${online ? "online" : "error"}`;
  ui.connectionText.textContent = online ? "Controller online" : "Controller unavailable";
  ui.apiStatus.textContent = online ? "CONNECTED" : "OFFLINE";
}

function createChart() {
  const context = document.querySelector("#temperatureChart").getContext("2d");
  chart = new Chart(context, {
    type: "line",
    data: { labels: [], datasets: [
      { label: "Actual temperature", data: [], borderColor: "#e5ff45", backgroundColor: "rgba(229,255,69,.05)", fill: true, borderWidth: 2, pointRadius: 0, tension: .32 },
      { label: "Target temperature", data: [], borderColor: "#73818a", borderDash: [5, 5], borderWidth: 1.5, pointRadius: 0, tension: .25 }
    ]},
    options: { responsive: true, maintainAspectRatio: false, animation: { duration: 350 }, interaction: { intersect: false, mode: "index" },
      plugins: { legend: { display: false }, tooltip: { backgroundColor: "#1a232a", borderColor: "#34414a", borderWidth: 1, titleFont: { family: "DM Mono" }, bodyFont: { family: "DM Mono" }, callbacks: { label: item => ` ${item.dataset.label}: ${item.formattedValue} °C` } } },
      scales: {
        x: { grid: { display: false }, border: { color: "#2a353d" }, ticks: { color: "#67757e", font: { family: "DM Mono", size: 9 }, maxTicksLimit: 8 }, title: { display: true, text: "TIME", color: "#53616a", font: { family: "DM Mono", size: 9 } } },
        y: { min: 0, suggestedMax: 250, grid: { color: "rgba(91,108,118,.16)" }, border: { display: false }, ticks: { color: "#67757e", font: { family: "DM Mono", size: 9 }, callback: value => `${value}°` }, title: { display: true, text: "TEMPERATURE / °C", color: "#53616a", font: { family: "DM Mono", size: 9 } } }
      }
    }
  });
}

function addChartPoint(status) {
  if (!chart) return;
  chart.data.labels.push(formatTime(status.elapsed_seconds));
  chart.data.datasets[0].data.push(status.temperature);
  chart.data.datasets[1].data.push(status.target_temperature);
  if (chart.data.labels.length > MAX_CHART_POINTS) {
    chart.data.labels.shift(); chart.data.datasets.forEach(dataset => dataset.data.shift());
  }
  chart.update("none");
}

function resetChart() {
  chart.data.labels.length = 0;
  chart.data.datasets.forEach(dataset => { dataset.data.length = 0; });
  lastElapsed = null;
  chart.update();
}

function showToast(message, isError = false) {
  clearTimeout(toastTimer);
  ui.toast.textContent = message;
  ui.toast.className = `toast show${isError ? " error" : ""}`;
  toastTimer = setTimeout(() => { ui.toast.className = "toast"; }, 2800);
}

const debug = {
  section: document.querySelector("#debugSection"), toggle: document.querySelector("#debugToggle"),
  content: document.querySelector("#debugContent"), terminal: document.querySelector("#debugTerminal"),
  empty: document.querySelector("#debugEmpty"), clear: document.querySelector("#debugClear"), download: document.querySelector("#debugDownload"),
  count: document.querySelector("#debugLineCount"), characterize: document.querySelector("#characterizeButton")
};

function appendDebugLines(lines) {
  if (!lines.length) return;
  debug.terminal.querySelector(".terminal-empty")?.remove();
  const fragment = document.createDocumentFragment();
  lines.forEach(line => {
    const element = document.createElement("div");
    element.className = "terminal-line";
    element.textContent = line.text;
    fragment.appendChild(element);
  });
  debug.terminal.appendChild(fragment);
  while (debug.terminal.children.length > 300) debug.terminal.firstElementChild.remove();
  debugLineCount += lines.length;
  debug.count.textContent = `${debugLineCount} ${debugLineCount === 1 ? "line" : "lines"}`;
  debug.terminal.scrollTop = debug.terminal.scrollHeight;
}

async function pollDebugLogs() {
  if (debug.toggle.getAttribute("aria-expanded") !== "true") return;
  try {
    const result = await apiRequest(`/logs?after=${debugCursor}`);
    appendDebugLines(result.lines);
    debugCursor = result.next_cursor;
  } catch (error) { console.error("Debug log polling failed", error); }
}

function toggleDebug() {
  const opening = debug.toggle.getAttribute("aria-expanded") !== "true";
  debug.toggle.setAttribute("aria-expanded", String(opening));
  debug.content.hidden = !opening;
  if (opening) pollDebugLogs();
}

function clearDebugOutput() {
  debug.terminal.replaceChildren();
  const placeholder = document.createElement("div");
  placeholder.className = "terminal-empty";
  placeholder.textContent = "Output cleared. Waiting for new backend output …";
  debug.terminal.appendChild(placeholder);
  debugLineCount = 0;
  debug.count.textContent = "0 lines";
}

function downloadDebugLog() {
  const lines = [...debug.terminal.querySelectorAll(".terminal-line")].map(line => line.textContent);
  if (!lines.length) {
    showToast("No log output available to download", true);
    return;
  }

  const blob = new Blob([`${lines.join("\n")}\n`], { type: "text/plain;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  link.href = url;
  link.download = `reflow-log-${timestamp}.txt`;
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
  showToast(`${lines.length} log ${lines.length === 1 ? "line" : "lines"} downloaded`);
}

const characterization = {
  backdrop: document.querySelector("#characterizationBackdrop"), close: document.querySelector("#characterizationClose"),
  start: document.querySelector("#characterizationStart"),
  download: document.querySelector("#characterizationDownload"),
  load: document.querySelector("#characterizationLoad"), file: document.querySelector("#characterizationFile"),
  downloadConfig: document.querySelector("#characterizationDownloadConfig"),
  state: document.querySelector("#characterizationState"), temperature: document.querySelector("#characterizationTemperature"),
  elapsed: document.querySelector("#characterizationElapsed"), curve: document.querySelector("#characterizationCurve"),
  axisMin: document.querySelector("#characterizationAxisMin"), axisMax: document.querySelector("#characterizationAxisMax"),
  empty: document.querySelector("#characterizationEmpty"), message: document.querySelector("#characterizationMessage"),
  heater: document.querySelector("#characterizationHeater"), terminal: document.querySelector("#characterizationTerminal"),
  quality: document.querySelector("#characterizationQuality"), summary: document.querySelector("#characterizationAnalysisSummary"),
  heatingChart: document.querySelector("#characterizationHeatingChart"), coolingChart: document.querySelector("#characterizationCoolingChart"), overshootChart: document.querySelector("#characterizationOvershootChart"),
  running: false, cursor: 0, samples: [], csvLines: ["time_ms,temperature_c,heater_output,phase"], analysis: null, sourceName: "characterization"
};

function drawCharacterizationChart() {
  if (!characterization.samples.length) return;
  const temperatures = characterization.samples.map(sample => sample.temperature);
  const minimum = Math.max(0, Math.floor((Math.min(...temperatures) - 5) / 10) * 10);
  const maximum = Math.ceil((Math.max(...temperatures) + 5) / 10) * 10;
  const span = Math.max(10, maximum - minimum);
  const lastTime = Math.max(1, characterization.samples.at(-1).time);
  characterization.curve.setAttribute("points", characterization.samples.map(sample => `${(sample.time / lastTime * 720).toFixed(1)},${(260 - (sample.temperature - minimum) / span * 260).toFixed(1)}`).join(" "));
  characterization.axisMin.textContent = `${minimum} °C`;
  characterization.axisMax.textContent = `${maximum} °C`;
}

function recordCharacterizationTemperature(timeMs, temperature) {
  if (!Number.isFinite(Number(temperature))) return;
  characterization.samples.push({ time: Number(timeMs) / 1000, temperature: Number(temperature) });
  if (characterization.samples.length > 300) characterization.samples.shift();
  characterization.temperature.textContent = `${Number(temperature).toFixed(1)} °C`;
  characterization.empty.hidden = true;
  drawCharacterizationChart();
}

function openCharacterization() {
  characterization.backdrop.hidden = false;
  document.body.classList.add("modal-open");
  pollCharacterization();
}

function closeCharacterization() {
  if (!characterization.running) {
    characterization.backdrop.hidden = true;
    document.body.classList.remove("modal-open");
  }
}

async function startCharacterization() {
  try {
    await apiRequest("/characterization/start", { method: "POST" });
    characterization.cursor = 0; characterization.samples = [];
    characterization.csvLines = ["time_ms,temperature_c,heater_output,phase"];
    characterization.curve.setAttribute("points", ""); characterization.empty.hidden = false;
    characterization.terminal.textContent = "time_ms,temperature_c,heater_output,phase";
    await pollCharacterization();
  } catch (error) { characterization.message.textContent = `Could not start measurement: ${error.message}`; }
}

async function abortCharacterization() {
  try {
    await apiRequest("/characterization/stop", { method: "POST" });
    await pollCharacterization();
  } catch (error) { characterization.message.textContent = `Could not abort measurement: ${error.message}`; }
}

function toggleCharacterization() {
  if (characterization.running) abortCharacterization();
  else startCharacterization();
}

function downloadCharacterizationCsv() {
  if (characterization.csvLines.length < 2) {
    characterization.message.textContent = "No measurement samples have reached this browser yet.";
    return;
  }
  const blob = new Blob([`${characterization.csvLines.join("\n")}\n`], { type: "text/csv" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url; link.download = "characterization.csv"; link.click();
  URL.revokeObjectURL(url);
}

function drawAnalysisChart(canvas, title, points, color) {
  const context = canvas.getContext("2d"); const width = canvas.width; const height = canvas.height;
  context.clearRect(0, 0, width, height); context.fillStyle = "#71808a"; context.font = "10px DM Mono"; context.fillText(title, 10, 16);
  if (!points.length) { context.fillText("Insufficient data", 10, 94); return; }
  const xs = points.map(point => point.x), ys = points.map(point => point.y);
  const xMin = Math.min(...xs), xMax = Math.max(...xs), yMin = Math.min(0, ...ys), yMax = Math.max(0, ...ys);
  const xSpan = Math.max(1, xMax - xMin), ySpan = Math.max(.1, yMax - yMin);
  const mapX = value => 28 + (value - xMin) / xSpan * (width - 38); const mapY = value => height - 24 - (value - yMin) / ySpan * (height - 48);
  context.strokeStyle = "#2a353d"; context.beginPath(); context.moveTo(28, 24); context.lineTo(28, height - 24); context.lineTo(width - 10, height - 24); context.stroke();
  context.strokeStyle = color; context.lineWidth = 2; context.beginPath(); points.forEach((point, index) => index ? context.lineTo(mapX(point.x), mapY(point.y)) : context.moveTo(mapX(point.x), mapY(point.y))); context.stroke();
  context.fillStyle = "#71808a"; context.fillText(`${xMin.toFixed(0)} °C`, 28, height - 8); context.fillText(`${xMax.toFixed(0)} °C`, width - 46, height - 8); context.fillText(`${yMax.toFixed(2)}`, 2, 30); context.fillText(`${yMin.toFixed(2)}`, 2, height - 26);
}

function renderAnalysis(result) {
  characterization.analysis = result;
  const { summary, dataQuality } = result;
  characterization.summary.textContent = `${dataQuality.usedSamples} usable / ${dataQuality.inputSamples} total samples · ${summary.minimumTemperatureC.toFixed(1)} °C → ${summary.maximumTemperatureC.toFixed(1)} °C · max heating ${summary.maximumHeatingRateCPerSecond ?? "—"} °C/s · max overshoot ${summary.maximumObservedOvershootC ?? "—"} °C`;
  characterization.quality.replaceChildren(...dataQuality.warnings.map(warning => { const item = document.createElement("li"); item.textContent = warning; return item; }));
  drawAnalysisChart(characterization.heatingChart, "HEATING RATE °C/s", result.heatingRate.map(point => ({ x: point.temperatureC, y: point.rateCPerSecond })), "#e5ff45");
  drawAnalysisChart(characterization.coolingChart, "PASSIVE COOLING °C/s", result.passiveCoolingRate.map(point => ({ x: point.temperatureC, y: point.rateCPerSecond })), "#78bbdf");
  drawAnalysisChart(characterization.overshootChart, "COAST OVERSHOOT °C", result.coastOvershoot.map(point => ({ x: point.temperatureC, y: point.overshootC })), "#ff9a65");
  characterization.downloadConfig.disabled = false;
}

async function loadCharacterizationCsv(event) {
  const [file] = event.target.files; if (!file) return;
  try {
    const analysis = window.CharacterizationAnalyzer.analyze(await file.text());
    characterization.sourceName = file.name.replace(/\.csv$/i, "") || "characterization";
    renderAnalysis(analysis.result); characterization.message.textContent = `Analyzed ${file.name}.`;
  } catch (error) { characterization.summary.textContent = `CSV analysis failed: ${error.message}`; characterization.downloadConfig.disabled = true; }
  finally { event.target.value = ""; }
}

function downloadConfigurationJson() {
  if (!characterization.analysis) return;
  const blob = new Blob([`${JSON.stringify(characterization.analysis, null, 2)}\n`], { type: "application/json" });
  const url = URL.createObjectURL(blob); const link = document.createElement("a");
  link.href = url; link.download = `${characterization.sourceName}.configuration.json`; link.click(); URL.revokeObjectURL(url);
}

function updateCharacterizationStatus(status) {
  characterization.running = status.running;
  characterization.state.textContent = String(status.phase).replaceAll("_", " ").toUpperCase();
  characterization.state.dataset.state = status.running ? "running" : status.phase === "aborted" || status.phase === "error" ? "aborted" : "ready";
  characterization.temperature.textContent = `${Number(status.temperature).toFixed(1)} °C`;
  characterization.heater.textContent = `HEATER ${status.heater_output ? "ON" : "OFF"}`;
  characterization.elapsed.textContent = formatTime(Math.floor(Number(status.elapsed_ms) / 1000));
  characterization.start.textContent = status.running ? "Abort measurement" : "Start measurement";
  characterization.start.classList.toggle("button-danger", status.running);
  if (status.error) characterization.message.textContent = `Measurement stopped: ${status.error.replaceAll("_", " ")}.`;
  else if (status.running) characterization.message.textContent = "Measurement in progress. The relay follows the automatic sequence.";
  else if (status.phase === "completed") characterization.message.textContent = "Measurement completed. The relay is disabled.";
}

async function pollCharacterization() {
  try {
    const status = await apiRequest("/characterization/status");
    updateCharacterizationStatus(status);
    const preview = await apiRequest(`/characterization/samples?after=${characterization.cursor}`);
    if (preview.lines.length) {
      for (const line of preview.lines) {
        characterization.terminal.textContent += `\n${line}`;
        characterization.csvLines.push(line);
        const [timeMs, temperature] = line.split(",");
        recordCharacterizationTemperature(timeMs, temperature);
      }
      const terminalLines = characterization.terminal.textContent.split("\n");
      if (terminalLines.length > 1001) {
        characterization.terminal.textContent = [terminalLines[0], ...terminalLines.slice(-1000)].join("\n");
      }
      characterization.terminal.scrollTop = characterization.terminal.scrollHeight;
    }
    characterization.cursor = preview.next_cursor;
  } catch (error) {
    if (!characterization.backdrop.hidden) characterization.message.textContent = `Characterization connection unavailable: ${error.message}`;
  }
}

async function pollStatus() {
  try {
    const status = await apiRequest("/status");
    if (!statusIsValid(status)) throw new Error("Invalid controller status response");
    updateStatus(status);
    setConnection(true);
  } catch (error) {
    setConnection(false);
    console.error("Controller status unavailable", error);
  }
}

async function startProcess() {
  ui.start.disabled = true;
  try { resetChart(); updateStatus(await apiRequest("/start", { method: "POST" })); showToast("Reflow profile started"); }
  catch (error) { showToast(error.message, true); }
  finally { pollStatus(); }
}

async function stopProcess() {
  ui.stop.disabled = true;
  try { updateStatus(await apiRequest("/stop", { method: "POST" })); showToast("Process stopped"); }
  catch (error) { showToast(error.message, true); }
  finally { pollStatus(); }
}

const profileUi = {
  select: document.querySelector("#profileSelect"), edit: document.querySelector("#profileEditButton"),
  backdrop: document.querySelector("#profileModalBackdrop"), modal: document.querySelector("#profileModal"),
  close: document.querySelector("#profileModalClose"), cancel: document.querySelector("#profileCancel"),
  form: document.querySelector("#profileEditorForm"), save: document.querySelector("#profileSave"),
  status: document.querySelector("#profileFormStatus"), explanation: document.querySelector("#profileExplanation"),
  name: document.querySelector("#profileName"), ramp: document.querySelector("#profileRampRate"),
  soakStart: document.querySelector("#profileSoakStart"), soakEnd: document.querySelector("#profileSoakEnd"),
  soakDuration: document.querySelector("#profileSoakDuration"), liquidus: document.querySelector("#profileLiquidus"),
  peak: document.querySelector("#profilePeak"), tal: document.querySelector("#profileTal"),
  previewRamp: document.querySelector("#previewRamp"), previewSoak: document.querySelector("#previewSoak"),
  previewTal: document.querySelector("#previewTal"), previewPeak: document.querySelector("#previewPeak")
};

const profileHighlightHelp = {
  ramp: "The highlighted ramps are limited by the maximum heating rate.",
  "soak-start": "Soak starts here, after the initial controlled heating ramp.",
  "soak-end": "Soak ends here before the temperature rises toward liquidus.",
  "soak-duration": "This highlighted interval is the complete linear soak phase.",
  liquidus: "The dashed line marks the temperature above which the solder is liquid.",
  peak: "This point is the maximum target temperature; no peak plateau is generated.",
  tal: "This highlighted interval is the total time above the liquidus temperature.",
  name: "The name identifies the compact profile configuration stored by the controller."
};

function readProfileForm() {
  return {
    name: profileUi.name.value.trim(),
    max_ramp_rate_c_per_s: Number(profileUi.ramp.value),
    soak: {
      start_temperature_c: Number(profileUi.soakStart.value),
      end_temperature_c: Number(profileUi.soakEnd.value),
      duration_s: Number(profileUi.soakDuration.value)
    },
    reflow: {
      liquidus_temperature_c: Number(profileUi.liquidus.value),
      peak_temperature_c: Number(profileUi.peak.value),
      time_above_liquidus_s: Number(profileUi.tal.value)
    }
  };
}

function writeProfileForm(profile) {
  profileUi.name.value = profile.name;
  profileUi.ramp.value = profile.max_ramp_rate_c_per_s;
  profileUi.soakStart.value = profile.soak.start_temperature_c;
  profileUi.soakEnd.value = profile.soak.end_temperature_c;
  profileUi.soakDuration.value = profile.soak.duration_s;
  profileUi.liquidus.value = profile.reflow.liquidus_temperature_c;
  profileUi.peak.value = profile.reflow.peak_temperature_c;
  profileUi.tal.value = profile.reflow.time_above_liquidus_s;
}

function populateProfileSelect(selectedName = activeProfileName) {
  profileUi.select.replaceChildren();
  availableProfiles.forEach(profile => {
    const option = document.createElement("option");
    option.value = profile.name;
    option.textContent = profile.name;
    profileUi.select.appendChild(option);
  });
  profileUi.select.value = selectedName;
  profileUi.select.disabled = availableProfiles.size === 0;
  profileUi.edit.disabled = availableProfiles.size === 0;
}

async function loadProfiles() {
  try {
    const response = await apiRequest("/profiles");
    availableProfiles.clear();
    response.profiles.forEach(profile => availableProfiles.set(profile.name, profile));
    activeProfileName = response.active_profile;
    populateProfileSelect();
  } catch (error) {
    profileUi.select.innerHTML = "<option>Profiles unavailable</option>";
    profileUi.select.disabled = true;
    profileUi.edit.disabled = true;
    showToast(`Could not load profiles: ${error.message}`, true);
  }
}

async function selectProfile() {
  const requestedName = profileUi.select.value;
  profileUi.select.disabled = true;
  try {
    await apiRequest("/profiles/active", { method: "PUT", body: JSON.stringify({ name: requestedName }) });
    activeProfileName = requestedName;
    showToast(`${requestedName} selected`);
  } catch (error) {
    profileUi.select.value = activeProfileName;
    showToast(error.message, true);
  } finally { profileUi.select.disabled = false; }
}

const profileOverlayPlugin = {
  id: "profileOverlay",
  afterDatasetsDraw(chartInstance) {
    const metadata = chartInstance.$profileMetadata;
    if (!metadata) return;
    const { ctx, chartArea, scales } = chartInstance;
    const x = scales.x;
    const y = scales.y;
    const highlight = chartInstance.$profileHighlight;
    const config = chartInstance.$profileConfig;
    const accent = "#e5ff45";
    ctx.save();

    const shade = (start, end, color = "rgba(229,255,69,.07)") => {
      ctx.fillStyle = color;
      ctx.fillRect(x.getPixelForValue(start), chartArea.top, x.getPixelForValue(end) - x.getPixelForValue(start), chartArea.bottom - chartArea.top);
    };
    if (highlight === "soak-duration") shade(metadata.rampToSoakEnd, metadata.soakEndTime);
    if (highlight === "tal") shade(metadata.liquidusUpTime, metadata.liquidusDownTime, "rgba(255,112,67,.10)");
    if (highlight === "ramp") {
      shade(0, metadata.rampToSoakEnd);
      shade(metadata.soakEndTime, metadata.liquidusUpTime);
    }

    ctx.font = "8px 'DM Mono'";
    ctx.textAlign = "center";
    const marker = (time, label, color = "#64737c") => {
      const px = x.getPixelForValue(time);
      ctx.strokeStyle = color;
      ctx.setLineDash([3, 5]);
      ctx.beginPath(); ctx.moveTo(px, chartArea.top + 7); ctx.lineTo(px, chartArea.bottom); ctx.stroke();
      ctx.setLineDash([]); ctx.fillStyle = color; ctx.fillText(label, px, chartArea.top + 9);
    };
    marker(metadata.rampToSoakEnd, "SOAK START", highlight === "soak-start" ? accent : "#66757d");
    marker(metadata.soakEndTime, "SOAK END", highlight === "soak-end" ? accent : "#66757d");
    marker(metadata.peakTime, "PEAK", highlight === "peak" ? accent : "#88959b");

    const bracket = (start, end, py, label, active) => {
      const left = x.getPixelForValue(start); const right = x.getPixelForValue(end);
      ctx.strokeStyle = active ? accent : "#526069"; ctx.fillStyle = active ? accent : "#718089";
      ctx.beginPath(); ctx.moveTo(left, py - 3); ctx.lineTo(left, py + 3); ctx.moveTo(left, py); ctx.lineTo(right, py); ctx.moveTo(right, py - 3); ctx.lineTo(right, py + 3); ctx.stroke();
      ctx.fillText(label, (left + right) / 2, py - 5);
    };
    bracket(metadata.rampToSoakEnd, metadata.soakEndTime, chartArea.bottom - 12, `SOAK ${config.soak.duration_s}s`, highlight === "soak-duration");
    bracket(metadata.liquidusUpTime, metadata.liquidusDownTime, y.getPixelForValue(config.reflow.liquidus_temperature_c) - 12, `TAL ${config.reflow.time_above_liquidus_s}s`, highlight === "tal");
    ctx.restore();
  }
};

function createProfilePreviewChart() {
  if (profilePreviewChart || typeof Chart === "undefined") return;
  profilePreviewChart = new Chart(document.querySelector("#profilePreviewChart"), {
    type: "line",
    plugins: [profileOverlayPlugin],
    data: { datasets: [
      { label: "Target profile", data: [], parsing: false, borderColor: "#e5ff45", borderWidth: 2.2, pointRadius: 0, tension: 0, order: 2 },
      { label: "Liquidus", data: [], parsing: false, borderColor: "#ff7043", borderWidth: 1, borderDash: [5, 5], pointRadius: 0, order: 3 },
      { label: "Selected parameter", data: [], parsing: false, borderColor: "#ffffff", borderWidth: 4, pointRadius: 0, tension: 0, order: 1 },
      { label: "Selected point", data: [], parsing: false, showLine: false, pointRadius: 6, pointHoverRadius: 6, pointBackgroundColor: "#e5ff45", pointBorderColor: "#0a1015", pointBorderWidth: 3, order: 0 }
    ]},
    options: {
      responsive: true, maintainAspectRatio: false, animation: { duration: 180 }, interaction: { intersect: false, mode: "nearest" },
      plugins: { legend: { display: false }, tooltip: { backgroundColor: "#151f26", borderColor: "#34414a", borderWidth: 1, callbacks: { label: item => ` ${item.dataset.label}: ${item.parsed.y.toFixed(1)} °C` } } },
      scales: {
        x: { type: "linear", min: 0, grid: { color: "rgba(104,121,130,.12)" }, border: { color: "#314049" }, title: { display: true, text: "TIME / s", color: "#65757d", font: { family: "DM Mono", size: 9 } }, ticks: { color: "#65757d", font: { family: "DM Mono", size: 8 }, maxTicksLimit: 8 } },
        y: { min: 20, suggestedMax: 260, grid: { color: "rgba(104,121,130,.14)" }, border: { display: false }, title: { display: true, text: "TEMPERATURE / °C", color: "#65757d", font: { family: "DM Mono", size: 9 } }, ticks: { color: "#65757d", font: { family: "DM Mono", size: 8 }, callback: value => `${value}°` } }
      }
    }
  });
}

function highlightData(curve, metadata, config, highlight) {
  const between = (start, end) => curve.filter(point => point.time_s >= start && point.time_s <= end).map(point => ({ x: point.time_s, y: point.temperature_c }));
  if (highlight === "soak-duration") return { line: between(metadata.rampToSoakEnd, metadata.soakEndTime), points: [] };
  if (highlight === "tal") return { line: between(metadata.liquidusUpTime, metadata.liquidusDownTime), points: [] };
  if (highlight === "ramp") {
    return { line: [...between(0, metadata.rampToSoakEnd), { x: NaN, y: NaN }, ...between(metadata.soakEndTime, metadata.liquidusUpTime)], points: [] };
  }
  const pointMap = {
    "soak-start": { x: metadata.rampToSoakEnd, y: config.soak.start_temperature_c },
    "soak-end": { x: metadata.soakEndTime, y: config.soak.end_temperature_c },
    peak: { x: metadata.peakTime, y: config.reflow.peak_temperature_c }
  };
  return { line: [], points: pointMap[highlight] ? [pointMap[highlight]] : [] };
}

function renderProfileEditor() {
  const config = readProfileForm();
  const validation = ReflowProfile.validateProfile(config);
  document.querySelectorAll("[data-error-for]").forEach(element => { element.textContent = validation.errors[element.dataset.errorFor] ?? ""; });
  document.querySelectorAll(".profile-field[data-profile-field]").forEach(field => field.classList.toggle("is-invalid", Boolean(validation.errors[field.dataset.profileField])));
  profileUi.save.disabled = !validation.valid;
  profileUi.status.textContent = validation.valid ? "" : "Fix the highlighted values before saving.";
  profileUi.explanation.textContent = activeProfileHighlight ? profileHighlightHelp[activeProfileHighlight] : "Focus or point at a parameter to see which part of the curve it controls.";
  if (!profilePreviewChart) return;

  if (!validation.valid) {
    profilePreviewChart.data.datasets.forEach(dataset => { dataset.data = []; });
    profilePreviewChart.$profileMetadata = null;
    profilePreviewChart.update();
    [profileUi.previewRamp, profileUi.previewSoak, profileUi.previewTal, profileUi.previewPeak].forEach(element => { element.textContent = "—"; });
    return;
  }

  const curve = ReflowProfile.generateProfile(config);
  const metadata = ReflowProfile.getProfileMetadata(config);
  const highlight = highlightData(curve, metadata, config, activeProfileHighlight);
  profilePreviewChart.data.datasets[0].data = curve.map(point => ({ x: point.time_s, y: point.temperature_c }));
  profilePreviewChart.data.datasets[1].data = [{ x: 0, y: config.reflow.liquidus_temperature_c }, { x: metadata.endTime, y: config.reflow.liquidus_temperature_c }];
  profilePreviewChart.data.datasets[1].borderColor = ["liquidus", "tal"].includes(activeProfileHighlight) ? "#ff9a65" : "#a9563c";
  profilePreviewChart.data.datasets[2].data = highlight.line;
  profilePreviewChart.data.datasets[3].data = highlight.points;
  profilePreviewChart.options.scales.x.max = Math.ceil(metadata.endTime / 10) * 10;
  profilePreviewChart.options.scales.y.max = Math.ceil((config.reflow.peak_temperature_c + 20) / 20) * 20;
  profilePreviewChart.$profileMetadata = metadata;
  profilePreviewChart.$profileHighlight = activeProfileHighlight;
  profilePreviewChart.$profileConfig = config;
  profilePreviewChart.update();
  profileUi.previewRamp.textContent = `${config.max_ramp_rate_c_per_s.toFixed(1)} °C/s`;
  profileUi.previewSoak.textContent = `${config.soak.duration_s} s`;
  profileUi.previewTal.textContent = `${config.reflow.time_above_liquidus_s} s`;
  profileUi.previewPeak.textContent = `${config.reflow.peak_temperature_c} °C`;
}

function setProfileHighlight(highlight) {
  activeProfileHighlight = highlight;
  document.querySelectorAll(".profile-field").forEach(field => field.classList.toggle("is-active", field.dataset.highlight === highlight));
  renderProfileEditor();
}

function openProfileEditor() {
  const profile = availableProfiles.get(profileUi.select.value);
  if (!profile) return;
  editedProfileName = profile.name;
  writeProfileForm(JSON.parse(JSON.stringify(profile)));
  profileUi.backdrop.hidden = false;
  document.body.classList.add("modal-open");
  createProfilePreviewChart();
  requestAnimationFrame(() => { profilePreviewChart?.resize(); renderProfileEditor(); profileUi.name.focus(); });
}

function closeProfileEditor() {
  profileUi.backdrop.hidden = true;
  document.body.classList.remove("modal-open");
  setProfileHighlight(null);
  profileUi.edit.focus();
}

async function saveProfile(event) {
  event.preventDefault();
  const config = readProfileForm();
  const validation = ReflowProfile.validateProfile(config);
  if (!validation.valid) { renderProfileEditor(); return; }
  profileUi.save.disabled = true;
  profileUi.status.textContent = "Saving profile …";
  try {
    const saved = await apiRequest("/profile", {
      method: "PUT",
      body: JSON.stringify({ original_name: editedProfileName, profile: config })
    });
    availableProfiles.delete(editedProfileName);
    availableProfiles.set(saved.name, saved);
    if (activeProfileName === editedProfileName) activeProfileName = saved.name;
    populateProfileSelect(activeProfileName);
    closeProfileEditor();
    showToast(`${saved.name} saved`);
  } catch (error) {
    profileUi.status.textContent = error.message;
    profileUi.save.disabled = false;
  }
}

const settings = {
  panel: document.querySelector("#settingsPanel"), backdrop: document.querySelector("#settingsBackdrop"),
  toggle: document.querySelector("#settingsToggle"), close: document.querySelector("#settingsClose"),
  form: document.querySelector("#settingsForm"), message: document.querySelector("#settingsMessage"),
  back: document.querySelector("#setupBack"), next: document.querySelector("#setupNext"),
  ssid: document.querySelector("#wifiSsid"), password: document.querySelector("#wifiPassword"),
  broker: document.querySelector("#mqttBroker"), port: document.querySelector("#mqttPort"),
  mqttUsername: document.querySelector("#mqttUsername"), mqttPassword: document.querySelector("#mqttPassword"),
  spiSck: document.querySelector("#spiSckPin"), spiMosi: document.querySelector("#spiMosiPin"), spiMiso: document.querySelector("#spiMisoPin"),
  relay: document.querySelector("#relayPin"), button: document.querySelector("#buttonPin"), led: document.querySelector("#ledPin"),
  pinMessage: document.querySelector("#pinValidationMessage")
};
let setupStep = 1;

function showSetupStep(step) {
  setupStep = Math.min(3, Math.max(1, step));
  document.querySelectorAll("[data-setup-step]").forEach(panel => { panel.hidden = Number(panel.dataset.setupStep) !== setupStep; });
  document.querySelectorAll("[data-step-indicator]").forEach(indicator => {
    const indicatorStep = Number(indicator.dataset.stepIndicator);
    indicator.classList.toggle("active", indicatorStep === setupStep);
    indicator.classList.toggle("complete", indicatorStep < setupStep);
  });
  document.querySelectorAll(".setup-progress > i").forEach((line, index) => line.classList.toggle("complete", index + 1 < setupStep));
  settings.back.hidden = setupStep === 1;
  settings.message.textContent = "";
  settings.pinMessage.textContent = "";
}

function fillSetupForm(config) {
  settings.ssid.value = config.wifi_ssid;
  settings.password.value = config.wifi_password;
  settings.broker.value = config.mqtt_broker;
  settings.port.value = config.mqtt_port;
  settings.mqttUsername.value = config.mqtt_username;
  settings.mqttPassword.value = config.mqtt_password;
  settings.spiSck.value = config.spi_sck_pin;
  settings.spiMosi.value = config.spi_mosi_pin;
  settings.spiMiso.value = config.spi_miso_pin;
  settings.relay.value = config.relay_pin;
  settings.button.value = config.button_pin;
  settings.led.value = config.led_pin;
}

async function openSettings() {
  settings.panel.setAttribute("aria-hidden", "false"); settings.toggle.setAttribute("aria-expanded", "true");
  settings.backdrop.hidden = false; document.body.classList.add("modal-open"); showSetupStep(1);
  settings.message.textContent = "Loading configuration …";
  try {
    const config = await apiRequest("/config");
    fillSetupForm(config);
    settings.message.textContent = "";
  } catch (error) { settings.message.textContent = `Error: ${error.message}`; }
}

function closeSettings() {
  settings.panel.setAttribute("aria-hidden", "true"); settings.toggle.setAttribute("aria-expanded", "false");
  settings.backdrop.hidden = true;
  if (profileUi.backdrop.hidden) document.body.classList.remove("modal-open");
  settings.toggle.focus();
}

function validateSetupStep() {
  const panel = document.querySelector(`[data-setup-step="${setupStep}"]`);
  const inputs = [...panel.querySelectorAll("input")];
  const invalidInput = inputs.find(input => !input.checkValidity());
  if (invalidInput) { invalidInput.reportValidity(); invalidInput.focus(); return false; }
  if (setupStep === 2) {
    const pins = inputs.map(input => Number(input.value));
    if (new Set(pins).size !== pins.length) {
      settings.pinMessage.textContent = "Every hardware function must use a unique GPIO pin.";
      return false;
    }
  }
  return true;
}

async function nextSetupStep() {
  if (!validateSetupStep()) return;
  if (setupStep < 3) showSetupStep(setupStep + 1);
  else {
    settings.next.disabled = true;
    await persistSetup(Boolean(settings.broker.value.trim()));
    settings.next.disabled = false;
  }
}

async function openSetupOnFirstRun() {
  try {
    const status = await apiRequest("/setup/status");
    if (status.setup_required) await openSettings();
  } catch (error) { console.error("Setup status could not be loaded", error); }
}

function buildSetupPayload(mqttEnabled) {
  return {
    wifi_ssid: settings.ssid.value,
    wifi_password: settings.password.value,
    mqtt_broker: mqttEnabled ? settings.broker.value : "",
    mqtt_port: mqttEnabled ? Number(settings.port.value) : 1883,
    mqtt_username: mqttEnabled ? settings.mqttUsername.value : "",
    mqtt_password: mqttEnabled ? settings.mqttPassword.value : "",
    mqtt_enabled: mqttEnabled,
    spi_sck_pin: Number(settings.spiSck.value),
    spi_mosi_pin: Number(settings.spiMosi.value),
    spi_miso_pin: Number(settings.spiMiso.value),
    relay_pin: Number(settings.relay.value),
    button_pin: Number(settings.button.value),
    led_pin: Number(settings.led.value),
    setup_complete: true
  };
}

async function persistSetup(mqttEnabled) {
  settings.message.textContent = "Saving …";
  try { await apiRequest("/config", { method: "PUT", body: JSON.stringify(buildSetupPayload(mqttEnabled)) }); closeSettings(); showToast(mqttEnabled ? "Installation completed with MQTT" : "Installation completed without MQTT"); }
  catch (error) { settings.message.textContent = `Error: ${error.message}`; }
}

async function saveSettings(event) {
  event.preventDefault();
  await nextSetupStep();
}

function initialize() {
  if (typeof Chart !== "undefined") createChart();
  debug.toggle.addEventListener("click", toggleDebug); debug.clear.addEventListener("click", clearDebugOutput);
  debug.download.addEventListener("click", downloadDebugLog);
  debug.characterize.addEventListener("click", openCharacterization);
  characterization.close.addEventListener("click", closeCharacterization);
  characterization.start.addEventListener("click", toggleCharacterization);
  characterization.download.addEventListener("click", downloadCharacterizationCsv);
  characterization.load.addEventListener("click", () => characterization.file.click());
  characterization.file.addEventListener("change", loadCharacterizationCsv);
  characterization.downloadConfig.addEventListener("click", downloadConfigurationJson);
  ui.start.disabled = true; ui.start.title = "Process control is not implemented yet";
  ui.stop.disabled = true;
  profileUi.select.innerHTML = "<option>Profiles not implemented</option>";
  profileUi.select.disabled = true; profileUi.edit.disabled = true;
  settings.toggle.disabled = true; settings.toggle.title = "Settings are not implemented yet";
  pollStatus(); setInterval(pollStatus, 1000); setInterval(pollDebugLogs, 1000); setInterval(pollCharacterization, 400);
}

initialize();
