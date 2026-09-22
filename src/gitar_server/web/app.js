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
  };

  const state = {
    config: null,
    wsOnline: false,
  };

  let flashTimer = null;
  let socket = null;
  let reconnectTimer = null;
  let reconnectAttempts = 0;

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

  function appendDetail(term, value) {
    const dt = document.createElement("dt");
    dt.textContent = term;
    const dd = document.createElement("dd");
    dd.textContent = value;
    elements.details.append(dt, dd);
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
  }

  async function init() {
    bindEvents();
    elements.volumeValue.textContent = `${elements.volume.value}%`;
    connectWs();
    try {
      await applyConfig(await api("/api/config"));
      await loadTones();
    } catch (error) {
      showError(error);
    }
    await refreshStatus();
  }

  init();
})();
