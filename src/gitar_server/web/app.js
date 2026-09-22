(() => {
  "use strict";

  const elements = {
    pill: document.getElementById("pill"),
    pillText: document.getElementById("pill-text"),
    version: document.getElementById("version"),
    flash: document.getElementById("flash"),
    source: document.getElementById("source"),
    channel: document.getElementById("channel"),
    output: document.getElementById("output"),
    latency: document.getElementById("latency"),
    saveConfig: document.getElementById("save-config"),
    connectDirect: document.getElementById("connect-direct"),
    connectAmp: document.getElementById("connect-amp"),
    disconnect: document.getElementById("disconnect"),
    volume: document.getElementById("volume"),
    volumeValue: document.getElementById("volume-value"),
    tone: document.getElementById("tone"),
    loadTone: document.getElementById("load-tone"),
    meterSeconds: document.getElementById("meter-seconds"),
    measure: document.getElementById("measure"),
    meterResult: document.getElementById("meter-result"),
    meterPeak: document.getElementById("meter-peak"),
    meterAdvice: document.getElementById("meter-advice"),
    details: document.getElementById("details"),
    engineHint: document.getElementById("engine-hint"),
    engineInput: document.getElementById("engine-input"),
    engineOutput: document.getElementById("engine-output"),
    engineStart: document.getElementById("engine-start"),
    engineStop: document.getElementById("engine-stop"),
    engineModel: document.getElementById("engine-model"),
    engineLoadModel: document.getElementById("engine-load-model"),
    engineClearModel: document.getElementById("engine-clear-model"),
    engineGain: document.getElementById("engine-gain"),
    engineGainValue: document.getElementById("engine-gain-value"),
    engineGateEnabled: document.getElementById("engine-gate-enabled"),
    engineGateThreshold: document.getElementById("engine-gate-threshold"),
    engineGateValue: document.getElementById("engine-gate-value"),
    engineEqLow: document.getElementById("engine-eq-low"),
    engineEqMid: document.getElementById("engine-eq-mid"),
    engineEqHigh: document.getElementById("engine-eq-high"),
    engineEqLowValue: document.getElementById("engine-eq-low-value"),
    engineEqMidValue: document.getElementById("engine-eq-mid-value"),
    engineEqHighValue: document.getElementById("engine-eq-high-value"),
    engineCab: document.getElementById("engine-cab"),
    engineLoadCab: document.getElementById("engine-load-cab"),
    engineClearCab: document.getElementById("engine-clear-cab"),
    enginePreset: document.getElementById("engine-preset"),
    engineLoadPreset: document.getElementById("engine-load-preset"),
    engineSavePreset: document.getElementById("engine-save-preset"),
    engineDeletePreset: document.getElementById("engine-delete-preset"),
    engineInputBar: document.getElementById("engine-input-bar"),
    engineOutputBar: document.getElementById("engine-output-bar"),
    engineInputPeak: document.getElementById("engine-input-peak"),
    engineOutputPeak: document.getElementById("engine-output-peak"),
    engineDetails: document.getElementById("engine-details"),
    engineTuner: document.getElementById("engine-tuner"),
    tunerNote: document.getElementById("tuner-note"),
    tunerCents: document.getElementById("tuner-cents"),
    tunerNeedle: document.getElementById("tuner-needle"),
  };

  const state = {
    config: null,
    wsOnline: false,
    engineAvailable: true,
    engineRunning: false,
    eqDragging: false,
  };

  const ENGINE_POLL_MS = 500;
  const TUNER_POLL_MS = 200;
  const LEVEL_MIN_DB = -60;
  const EQ_DEBOUNCE_MS = 200;
  const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

  let flashTimer = null;
  let socket = null;
  let reconnectTimer = null;
  let reconnectAttempts = 0;
  let engineTimer = null;
  let tunerTimer = null;
  let eqTimer = null;

  async function api(path, options = {}) {
    const { method = "GET", body } = options;
    const response = await fetch(path, {
      method,
      headers: body === undefined ? {} : { "Content-Type": "application/json" },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    if (!response.ok) {
      let detail = `request failed (${response.status})`;
      try {
        const payload = await response.json();
        if (payload && typeof payload.detail === "string") {
          detail = payload.detail;
        }
      } catch {
        // Non-JSON error body; keep the status-based message.
      }
      throw new Error(detail);
    }
    return response.json();
  }

  function flash(message, kind) {
    elements.flash.textContent = message;
    elements.flash.dataset.kind = kind;
    elements.flash.hidden = false;
    window.clearTimeout(flashTimer);
    flashTimer = window.setTimeout(() => {
      elements.flash.hidden = true;
    }, 5000);
  }

  function showError(error) {
    flash(error instanceof Error ? error.message : String(error), "error");
  }

  function fillDeviceSelect(select, devices, placeholder) {
    const options = devices.map((device) => ({
      value: device.name,
      label: device.description || device.name,
    }));
    fillSelect(select, options, placeholder);
  }

  function fillTextSelect(select, values, placeholder) {
    fillSelect(
      select,
      values.map((value) => ({ value, label: value })),
      placeholder,
    );
  }

  function fillSelect(select, options, placeholder) {
    select.replaceChildren();
    const blank = document.createElement("option");
    blank.value = "";
    blank.textContent = placeholder;
    select.append(blank);
    for (const { value, label } of options) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = label;
      select.append(option);
    }
  }

  function setSelectValue(select, value) {
    if (value && !Array.from(select.options).some((option) => option.value === value)) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = value;
      select.append(option);
    }
    select.value = value || "";
  }

  async function loadDevices() {
    const [sources, sinks] = await Promise.all([
      api("/api/devices?kind=source"),
      api("/api/devices?kind=sink"),
    ]);
    fillDeviceSelect(elements.source, sources.devices, "— select input —");
    fillDeviceSelect(elements.output, sinks.devices, "— select output —");
  }

  async function loadPorts(device, selected = "") {
    if (!device) {
      fillTextSelect(elements.channel, [], "— select input first —");
      return;
    }
    const body = await api(`/api/ports?device=${encodeURIComponent(device)}&kind=source`);
    fillTextSelect(elements.channel, body.ports, "— auto / default —");
    setSelectValue(elements.channel, selected);
  }

  async function applyConfig(config) {
    state.config = config;
    await loadDevices();
    setSelectValue(elements.source, config.input);
    setSelectValue(elements.output, config.output);
    elements.latency.value = config.latency || "128/48000";
    await loadPorts(config.input, config.channel);
  }

  function renderHealth(health) {
    elements.version.textContent = `v${health.version} · ${health.backend}`;
  }

  function renderStatus(status) {
    renderPill(status, true);
    renderDetails(status);
  }

  function renderPill(status, online) {
    if (!online) {
      elements.pill.dataset.state = "offline";
      elements.pillText.textContent = "offline";
      return;
    }
    const engine = status.engine_running ? "engine on" : "engine off";
    elements.pill.dataset.state = status.route;
    elements.pillText.textContent = `${status.route} · ${engine}`;
  }

  function renderDetails(status) {
    elements.details.replaceChildren();
    appendDetail("route", status.route);
    appendDetail("connected", status.connected ? "yes" : "no");
    appendDetail("engine_running", status.engine_running ? "yes" : "no");
    for (const [key, value] of Object.entries(status.details || {})) {
      appendDetail(key, String(value));
    }
  }

  function appendDefinition(list, term, value) {
    const dt = document.createElement("dt");
    dt.textContent = term;
    const dd = document.createElement("dd");
    dd.textContent = value;
    list.append(dt, dd);
  }

  function appendDetail(term, value) {
    appendDefinition(elements.details, term, value);
  }

  function levelForAdvice(advice) {
    const text = advice.toLowerCase();
    if (text.includes("good")) return "good";
    if (text.includes("low") || text.includes("no signal")) return "warn";
    return "bad";
  }

  async function refreshStatus() {
    try {
      renderStatus(await api("/api/status"));
    } catch (error) {
      showError(error);
    }
  }

  async function saveConfig() {
    const payload = {
      ...(state.config || {}),
      input: elements.source.value,
      channel: elements.channel.value,
      output: elements.output.value,
      latency: elements.latency.value,
    };
    try {
      state.config = await api("/api/config", { method: "PUT", body: payload });
      flash("Configuration saved.", "info");
    } catch (error) {
      showError(error);
    }
  }

  async function setRoute(mode) {
    try {
      renderStatus(await api("/api/connect", { method: "POST", body: { mode } }));
    } catch (error) {
      showError(error);
    }
  }

  async function disconnect() {
    try {
      renderStatus(await api("/api/disconnect", { method: "POST" }));
    } catch (error) {
      showError(error);
    }
  }

  async function setVolume(percent) {
    try {
      await api("/api/volume", { method: "POST", body: { percent } });
    } catch (error) {
      showError(error);
    }
  }

  async function loadTones() {
    try {
      const body = await api("/api/tones");
      fillTextSelect(elements.tone, body.tones, "— select tone —");
    } catch (error) {
      showError(error);
    }
  }

  async function loadTone() {
    const tone = elements.tone.value;
    if (!tone) {
      flash("Select a tone first.", "error");
      return;
    }
    try {
      await api("/api/tone", { method: "POST", body: { tone } });
      flash(`Loaded tone "${tone}".`, "info");
    } catch (error) {
      showError(error);
    }
  }

  async function measure() {
    const seconds = Number(elements.meterSeconds.value) || 10;
    elements.measure.disabled = true;
    try {
      const reading = await api("/api/meter", { method: "POST", body: { seconds } });
      elements.meterPeak.textContent = `${reading.peak_dbfs.toFixed(1)} dBFS`;
      elements.meterAdvice.textContent = reading.advice;
      elements.meterResult.dataset.level = levelForAdvice(reading.advice);
      elements.meterResult.hidden = false;
    } catch (error) {
      showError(error);
    } finally {
      elements.measure.disabled = false;
    }
  }

  function formatSampleRate(rate) {
    if (typeof rate !== "number" || !rate) return "—";
    const khz = rate / 1000;
    return Number.isInteger(khz) ? `${khz} kHz` : `${khz.toFixed(1)} kHz`;
  }

  function formatDecimal(value, digits) {
    return typeof value === "number" && Number.isFinite(value) ? value.toFixed(digits) : "—";
  }

  function formatPeak(db) {
    return `${formatDecimal(db, 1)} dB`;
  }

  function levelPercent(db) {
    if (typeof db !== "number" || !Number.isFinite(db)) return 0;
    const clamped = Math.max(LEVEL_MIN_DB, Math.min(0, db));
    return ((clamped - LEVEL_MIN_DB) / -LEVEL_MIN_DB) * 100;
  }

  function fillEngineDeviceSelect(select, devices, placeholder) {
    const options = devices.map((device) => ({
      value: device.name,
      label: device.is_default ? `${device.name} (default)` : device.name,
    }));
    fillSelect(select, options, placeholder);
  }

  function fillModelSelect(models) {
    const options = models.map((model) => ({
      value: model.path,
      label: `${model.name} · ${model.architecture} · ${formatSampleRate(model.sample_rate)}`,
    }));
    fillSelect(elements.engineModel, options, "— select model —");
  }

  function applyEngineControls() {
    const available = state.engineAvailable;
    const running = state.engineRunning;
    elements.engineStart.disabled = !available || running;
    elements.engineStop.disabled = !available || !running;
    elements.engineLoadModel.disabled = !available;
    elements.engineClearModel.disabled = !available;
    elements.engineModel.disabled = !available;
    elements.engineGain.disabled = !available;
    elements.engineGateEnabled.disabled = !available;
    elements.engineGateThreshold.disabled = !available;
    elements.engineEqLow.disabled = !available;
    elements.engineEqMid.disabled = !available;
    elements.engineEqHigh.disabled = !available;
    elements.engineCab.disabled = !available;
    elements.engineLoadCab.disabled = !available;
    elements.engineClearCab.disabled = !available;
    elements.enginePreset.disabled = !available;
    elements.engineLoadPreset.disabled = !available;
    elements.engineSavePreset.disabled = !available;
    elements.engineDeletePreset.disabled = !available;
    elements.engineHint.hidden = available;
  }

  function renderGainValue(gain) {
    elements.engineGainValue.textContent = `${formatDecimal(gain, 2)}×`;
  }

  function renderGateValue(threshold) {
    elements.engineGateValue.textContent = `${formatDecimal(threshold, 0)} dB`;
  }

  function renderEqValues() {
    elements.engineEqLowValue.textContent = `${formatDecimal(Number(elements.engineEqLow.value), 1)} dB`;
    elements.engineEqMidValue.textContent = `${formatDecimal(Number(elements.engineEqMid.value), 1)} dB`;
    elements.engineEqHighValue.textContent = `${formatDecimal(Number(elements.engineEqHigh.value), 1)} dB`;
  }

  function eqBusy() {
    return (
      state.eqDragging ||
      document.activeElement === elements.engineEqLow ||
      document.activeElement === elements.engineEqMid ||
      document.activeElement === elements.engineEqHigh
    );
  }

  function renderEngineDetails(status) {
    const details = elements.engineDetails;
    details.replaceChildren();
    appendDefinition(details, "running", status.running ? "yes" : "no");
    appendDefinition(
      details,
      "model",
      status.model_loaded ? String(status.model_path || "loaded") : "none",
    );
    appendDefinition(
      details,
      "cabinet",
      status.cab_ir_loaded ? String(status.cab_ir_path || "loaded") : "none",
    );
    appendDefinition(details, "latency", `${formatDecimal(status.latency_ms, 1)} ms`);
    appendDefinition(details, "sample_rate", formatSampleRate(status.sample_rate));
    appendDefinition(
      details,
      "period",
      typeof status.period_frames === "number" ? `${status.period_frames} frames` : "—",
    );
    appendDefinition(details, "input_peak", formatPeak(status.input_peak_db));
    appendDefinition(details, "output_peak", formatPeak(status.output_peak_db));
    appendDefinition(details, "overruns", String(status.overrun_frames ?? 0));
    appendDefinition(details, "underruns", String(status.underrun_frames ?? 0));
  }

  function renderEngineLevels(status) {
    elements.engineInputPeak.textContent = formatPeak(status.input_peak_db);
    elements.engineOutputPeak.textContent = formatPeak(status.output_peak_db);
    elements.engineInputBar.style.width = `${levelPercent(status.input_peak_db)}%`;
    elements.engineOutputBar.style.width = `${levelPercent(status.output_peak_db)}%`;
  }

  function hzToNote(hz, a4 = 440) {
    if (!Number.isFinite(hz) || hz <= 0) return null;
    const midi = 69 + 12 * Math.log2(hz / a4);
    const nearest = Math.floor(midi + 0.5);
    return {
      name: NOTE_NAMES[((nearest % 12) + 12) % 12],
      octave: Math.floor(nearest / 12) - 1,
      cents: 100 * (midi - nearest),
    };
  }

  function renderTuner(status) {
    const hz = status && typeof status.pitch_hz === "number" ? status.pitch_hz : 0;
    const note = hzToNote(hz);
    if (!note) {
      elements.tunerNote.textContent = "—";
      elements.tunerCents.textContent = "";
      elements.tunerNeedle.style.left = "50%";
      elements.engineTuner.dataset.state = "idle";
      return;
    }
    const cents = Math.max(-50, Math.min(50, note.cents));
    elements.tunerNote.textContent = `${note.name}${note.octave}`;
    elements.tunerCents.textContent = `${cents >= 0 ? "+" : ""}${cents.toFixed(0)} cents`;
    elements.tunerNeedle.style.left = `${50 + cents}%`;
    elements.engineTuner.dataset.state = Math.abs(cents) <= 5 ? "in-tune" : "active";
  }

  function renderEngineStatus(status) {
    state.engineAvailable = true;
    state.engineRunning = Boolean(status.running);
    applyEngineControls();
    renderEngineDetails(status);
    renderEngineLevels(status);
    renderTuner(status);
    if (typeof status.gain === "number") {
      elements.engineGain.value = String(status.gain);
      renderGainValue(status.gain);
    }
    if (typeof status.gate_enabled === "boolean") {
      elements.engineGateEnabled.checked = status.gate_enabled;
    }
    if (typeof status.gate_threshold_db === "number") {
      elements.engineGateThreshold.value = String(status.gate_threshold_db);
      renderGateValue(status.gate_threshold_db);
    }
    if (!eqBusy()) {
      if (typeof status.eq_low_db === "number") {
        elements.engineEqLow.value = String(status.eq_low_db);
      }
      if (typeof status.eq_mid_db === "number") {
        elements.engineEqMid.value = String(status.eq_mid_db);
      }
      if (typeof status.eq_high_db === "number") {
        elements.engineEqHigh.value = String(status.eq_high_db);
      }
      renderEqValues();
    }
    if (status.cab_ir_loaded && typeof status.cab_ir_path === "string" && status.cab_ir_path) {
      setSelectValue(elements.engineCab, status.cab_ir_path);
    }
  }

  function renderEngineOffline() {
    state.engineAvailable = false;
    state.engineRunning = false;
    applyEngineControls();
    elements.engineDetails.replaceChildren();
    elements.engineInputBar.style.width = "0%";
    elements.engineOutputBar.style.width = "0%";
    elements.engineInputPeak.textContent = "—";
    elements.engineOutputPeak.textContent = "—";
    renderTuner(null);
  }

  async function refreshEngineStatus() {
    try {
      renderEngineStatus(await api("/api/engine/status"));
    } catch {
      renderEngineOffline();
    }
  }

  // The tuner needs a faster cadence than the control poll, so it keeps its own
  // timer and only touches the readout.
  async function refreshTunerStatus() {
    try {
      renderTuner(await api("/api/engine/status"));
    } catch {
      renderTuner(null);
    }
  }

  function startEnginePolling() {
    if (engineTimer !== null) return;
    engineTimer = window.setInterval(refreshEngineStatus, ENGINE_POLL_MS);
  }

  function stopEnginePolling() {
    window.clearInterval(engineTimer);
    engineTimer = null;
  }

  function startTunerPolling() {
    if (tunerTimer !== null) return;
    tunerTimer = window.setInterval(refreshTunerStatus, TUNER_POLL_MS);
  }

  function stopTunerPolling() {
    window.clearInterval(tunerTimer);
    tunerTimer = null;
  }

  async function loadEngineDevices() {
    try {
      const body = await api("/api/engine/devices");
      fillEngineDeviceSelect(
        elements.engineInput,
        body.devices.filter((device) => device.is_input),
        "— default input —",
      );
      fillEngineDeviceSelect(
        elements.engineOutput,
        body.devices.filter((device) => device.is_output),
        "— default output —",
      );
      state.engineAvailable = true;
      applyEngineControls();
    } catch {
      renderEngineOffline();
    }
  }

  async function loadModels() {
    try {
      const body = await api("/api/models");
      fillModelSelect(body.models);
    } catch (error) {
      showError(error);
    }
  }

  async function startEngine() {
    try {
      const status = await api("/api/engine/start", {
        method: "POST",
        body: {
          input_device: elements.engineInput.value || undefined,
          output_device: elements.engineOutput.value || undefined,
          gain: Number(elements.engineGain.value),
          gate_enabled: elements.engineGateEnabled.checked,
          gate_threshold_db: Number(elements.engineGateThreshold.value),
        },
      });
      renderEngineStatus(status);
      flash("Engine started.", "info");
    } catch (error) {
      showError(error);
    }
  }

  async function stopEngine() {
    try {
      renderEngineStatus(await api("/api/engine/stop", { method: "POST" }));
      flash("Engine stopped.", "info");
    } catch (error) {
      showError(error);
    }
  }

  async function loadEngineModel() {
    const path = elements.engineModel.value;
    if (!path) {
      flash("Select a model first.", "error");
      return;
    }
    try {
      renderEngineStatus(await api("/api/engine/model", { method: "POST", body: { path } }));
      flash("Model loaded.", "info");
    } catch (error) {
      showError(error);
    }
  }

  async function clearEngineModel() {
    try {
      renderEngineStatus(await api("/api/engine/model", { method: "POST", body: { path: "" } }));
      flash("Model cleared.", "info");
    } catch (error) {
      showError(error);
    }
  }

  async function setEngineGain(gain) {
    try {
      renderEngineStatus(await api("/api/engine/gain", { method: "POST", body: { gain } }));
    } catch (error) {
      showError(error);
    }
  }

  async function setEngineGate() {
    try {
      const status = await api("/api/engine/gate", {
        method: "POST",
        body: {
          enabled: elements.engineGateEnabled.checked,
          threshold_db: Number(elements.engineGateThreshold.value),
        },
      });
      renderEngineStatus(status);
    } catch (error) {
      showError(error);
    }
  }

  async function setEngineEq() {
    try {
      const status = await api("/api/engine/eq", {
        method: "POST",
        body: {
          low_db: Number(elements.engineEqLow.value),
          mid_db: Number(elements.engineEqMid.value),
          high_db: Number(elements.engineEqHigh.value),
        },
      });
      renderEngineStatus(status);
    } catch (error) {
      showError(error);
    }
  }

  function scheduleEngineEq() {
    window.clearTimeout(eqTimer);
    eqTimer = window.setTimeout(setEngineEq, EQ_DEBOUNCE_MS);
  }

  async function loadCabs() {
    try {
      const body = await api("/api/cabs");
      const options = body.cabs.map((cab) => ({ value: cab.path, label: cab.name }));
      fillSelect(elements.engineCab, options, "— select cabinet —");
    } catch (error) {
      showError(error);
    }
  }

  async function loadEngineCab() {
    const path = elements.engineCab.value;
    if (!path) {
      flash("Select a cabinet first.", "error");
      return;
    }
    try {
      renderEngineStatus(await api("/api/engine/cab", { method: "POST", body: { path } }));
      flash("Cabinet loaded.", "info");
    } catch (error) {
      showError(error);
    }
  }

  async function clearEngineCab() {
    try {
      renderEngineStatus(await api("/api/engine/cab", { method: "POST", body: { path: "" } }));
      flash("Cabinet cleared.", "info");
    } catch (error) {
      showError(error);
    }
  }

  async function loadPresets() {
    try {
      const body = await api("/api/presets");
      fillTextSelect(elements.enginePreset, body.presets, "— select preset —");
    } catch (error) {
      showError(error);
    }
  }

  function presetFromStatus(name, status) {
    return {
      name,
      model_path: typeof status.model_path === "string" ? status.model_path : "",
      cab_ir_path: typeof status.cab_ir_path === "string" ? status.cab_ir_path : "",
      gain: typeof status.gain === "number" ? status.gain : 1.0,
      gate_enabled: Boolean(status.gate_enabled),
      gate_threshold_db:
        typeof status.gate_threshold_db === "number" ? status.gate_threshold_db : -60.0,
      eq_low_db: typeof status.eq_low_db === "number" ? status.eq_low_db : 0.0,
      eq_mid_db: typeof status.eq_mid_db === "number" ? status.eq_mid_db : 0.0,
      eq_high_db: typeof status.eq_high_db === "number" ? status.eq_high_db : 0.0,
    };
  }

  async function loadPreset() {
    const name = elements.enginePreset.value;
    if (!name) {
      flash("Select a preset first.", "error");
      return;
    }
    try {
      const status = await api(`/api/presets/${encodeURIComponent(name)}/apply`, {
        method: "POST",
      });
      renderEngineStatus(status);
      flash(`Loaded preset "${name}".`, "info");
    } catch (error) {
      showError(error);
    }
  }

  async function savePreset() {
    const name = window.prompt("Preset name:");
    if (!name || !name.trim()) return;
    try {
      const status = await api("/api/engine/status");
      const saved = await api("/api/presets", {
        method: "POST",
        body: presetFromStatus(name.trim(), status),
      });
      await loadPresets();
      setSelectValue(elements.enginePreset, saved.name);
      flash(`Saved preset "${saved.name}".`, "info");
    } catch (error) {
      showError(error);
    }
  }

  async function deletePreset() {
    const name = elements.enginePreset.value;
    if (!name) {
      flash("Select a preset first.", "error");
      return;
    }
    if (!window.confirm(`Delete preset "${name}"?`)) return;
    try {
      await api(`/api/presets/${encodeURIComponent(name)}`, { method: "DELETE" });
      await loadPresets();
      flash(`Deleted preset "${name}".`, "info");
    } catch (error) {
      showError(error);
    }
  }

  function connectWs() {
    const protocol = window.location.protocol === "https:" ? "wss" : "ws";
    socket = new WebSocket(`${protocol}://${window.location.host}/api/ws`);

    socket.addEventListener("open", () => {
      reconnectAttempts = 0;
    });

    socket.addEventListener("message", (event) => {
      const message = JSON.parse(event.data);
      if (message.health) renderHealth(message.health);
      if (message.status) renderStatus(message.status);
    });

    socket.addEventListener("close", () => {
      state.wsOnline = false;
      renderPill(null, false);
      scheduleReconnect();
    });

    socket.addEventListener("error", () => {
      socket.close();
    });
  }

  function scheduleReconnect() {
    reconnectAttempts += 1;
    const delay = Math.min(1000 * 2 ** (reconnectAttempts - 1), 15000);
    window.clearTimeout(reconnectTimer);
    reconnectTimer = window.setTimeout(connectWs, delay);
  }

  function bindEvents() {
    elements.saveConfig.addEventListener("click", saveConfig);
    elements.connectDirect.addEventListener("click", () => setRoute("direct"));
    elements.connectAmp.addEventListener("click", () => setRoute("amp"));
    elements.disconnect.addEventListener("click", disconnect);

    elements.source.addEventListener("change", () => {
      loadPorts(elements.source.value).catch(showError);
    });

    elements.volume.addEventListener("input", () => {
      elements.volumeValue.textContent = `${elements.volume.value}%`;
    });
    elements.volume.addEventListener("change", () => {
      setVolume(Number(elements.volume.value));
    });

    elements.loadTone.addEventListener("click", loadTone);
    elements.measure.addEventListener("click", measure);

    elements.engineStart.addEventListener("click", startEngine);
    elements.engineStop.addEventListener("click", stopEngine);
    elements.engineLoadModel.addEventListener("click", loadEngineModel);
    elements.engineClearModel.addEventListener("click", clearEngineModel);

    elements.engineGain.addEventListener("input", () => {
      renderGainValue(Number(elements.engineGain.value));
    });
    elements.engineGain.addEventListener("change", () => {
      setEngineGain(Number(elements.engineGain.value));
    });

    elements.engineGateThreshold.addEventListener("input", () => {
      renderGateValue(Number(elements.engineGateThreshold.value));
    });
    elements.engineGateThreshold.addEventListener("change", setEngineGate);
    elements.engineGateEnabled.addEventListener("change", setEngineGate);

    for (const slider of [
      elements.engineEqLow,
      elements.engineEqMid,
      elements.engineEqHigh,
    ]) {
      slider.addEventListener("pointerdown", () => {
        state.eqDragging = true;
      });
      slider.addEventListener("input", () => {
        renderEqValues();
        scheduleEngineEq();
      });
      slider.addEventListener("change", () => {
        window.clearTimeout(eqTimer);
        setEngineEq();
      });
    }
    window.addEventListener("pointerup", () => {
      state.eqDragging = false;
    });

    elements.engineLoadCab.addEventListener("click", loadEngineCab);
    elements.engineClearCab.addEventListener("click", clearEngineCab);
    elements.engineLoadPreset.addEventListener("click", loadPreset);
    elements.engineSavePreset.addEventListener("click", savePreset);
    elements.engineDeletePreset.addEventListener("click", deletePreset);

    document.addEventListener("visibilitychange", () => {
      if (document.hidden) {
        stopEnginePolling();
        stopTunerPolling();
      } else {
        refreshEngineStatus();
        refreshTunerStatus();
        startEnginePolling();
        startTunerPolling();
      }
    });
  }

  async function init() {
    bindEvents();
    elements.volumeValue.textContent = `${elements.volume.value}%`;
    renderGainValue(Number(elements.engineGain.value));
    renderGateValue(Number(elements.engineGateThreshold.value));
    renderEqValues();
    connectWs();
    startEnginePolling();
    startTunerPolling();
    try {
      await applyConfig(await api("/api/config"));
      await loadTones();
    } catch (error) {
      showError(error);
    }
    await loadEngineDevices();
    await loadModels();
    await loadCabs();
    await loadPresets();
    await refreshStatus();
  }

  init();
})();
