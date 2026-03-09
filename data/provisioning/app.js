const webStatusEl = document.getElementById('webStatus');
const nodesEl = document.getElementById('nodes');
const addSensorBtn = document.getElementById('addSensorBtn');
const closePairingBtn = document.getElementById('closePairingBtn');
const pairingBannerEl = document.getElementById('pairingBanner');
const calibrationBannerEl = document.getElementById('calibrationBanner');
const tabsEl = document.querySelector('.tabs');
const tabButtons = Array.from(document.querySelectorAll('.tab-btn'));
const tabPanels = Array.from(document.querySelectorAll('.tab-panel'));
const homeOnlineEl = document.getElementById('homeOnline');
const homeMoistureEl = document.getElementById('homeMoisture');
const homePairingEl = document.getElementById('homePairing');
const homeUptimeEl = document.getElementById('homeUptime');
const homeModeFormEl = document.getElementById('homeModeForm');
const homeModeSaveBtnEl = document.getElementById('homeModeSaveBtn');
const homeModeStatusEl = document.getElementById('homeModeStatus');
const homeManualActionsEl = document.querySelector('.home-manual-actions');
const manualStartBtnEl = document.getElementById('manualStartBtn');
const manualStopBtnEl = document.getElementById('manualStopBtn');
const homeManualStatusEl = document.getElementById('homeManualStatus');
const homeControlLockoutStatusEl = document.getElementById('homeControlLockoutStatus');
const unitApSsidEl = document.getElementById('unitApSsid');
const unitFirmwareEl = document.getElementById('unitFirmware');
const unitUptimeEl = document.getElementById('unitUptime');
const unitPasswordStateEl = document.getElementById('unitPasswordState');
const unitConfigFormEl = document.getElementById('unitConfigForm');
const unitNameInputEl = document.getElementById('unitNameInput');
const unitNameSaveBtnEl = document.getElementById('unitNameSaveBtn');
const unitPasswordInputEl = document.getElementById('unitPasswordInput');
const unitPasswordSaveBtnEl = document.getElementById('unitPasswordSaveBtn');
const unitConfigStatusEl = document.getElementById('unitConfigStatus');
const factoryResetConfirmInputEl = document.getElementById('factoryResetConfirmInput');
const factoryResetBtnEl = document.getElementById('factoryResetBtn');
const factoryResetStatusEl = document.getElementById('factoryResetStatus');
const calibrationStateByNode = new Map();
let latestNodes = [];
let activeCalibration = null;
let pairingRemainingSec = 0;
let persistedIrrigationMode = 'AUTO';
let pendingIrrigationMode = 'AUTO';
let isManualIrrigationActive = false;
let persistedUnitName = '';
let pendingUnitName = '';
let unitNameInitialized = false;
const CAL_PROMPT_TIMEOUT_MS = 20000;
const CAL_ERROR_HIDE_MS = 5000;
const CAL_INFO_HIDE_MS = 5000;

function render(el, data) {
  if (!el) {
    return;
  }
  el.textContent = JSON.stringify(data, null, 2);
}

function formatBatteryVolts(mv) {
  if (!Number.isFinite(mv) || mv <= 0) return 'N/A';
  return `${(mv / 1000).toFixed(2)}V`;
}

function formatMoisture(permille) {
  if (!Number.isFinite(permille)) return 'N/A';
  return `${(permille / 10).toFixed(1)}%`;
}

function formatDuration(totalSec) {
  if (!Number.isFinite(totalSec) || totalSec < 0) {
    return '-';
  }
  const seconds = Math.floor(totalSec);
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  if (hours > 0) {
    return `${hours}h ${minutes}m`;
  }
  return `${minutes}m`;
}

function normalizeIrrigationMode(mode) {
  const normalized = String(mode || '').toUpperCase();
  if (normalized === 'AUTO' || normalized === 'MANUAL' || normalized === 'OFF') {
    return normalized;
  }
  return 'AUTO';
}

function isHomeModeDirty() {
  return normalizeIrrigationMode(pendingIrrigationMode) !== normalizeIrrigationMode(persistedIrrigationMode);
}

function setHomeModeStatus(text, isError = false) {
  if (!homeModeStatusEl) {
    return;
  }
  homeModeStatusEl.textContent = text;
  homeModeStatusEl.classList.toggle('error', isError);
}

function setHomeManualStatus(text, isError = false) {
  if (!homeManualStatusEl) {
    return;
  }
  homeManualStatusEl.textContent = text;
  homeManualStatusEl.classList.toggle('error', isError);
}

function setHomeControlLockoutStatus(text, isError = false) {
  if (!homeControlLockoutStatusEl) {
    return;
  }
  homeControlLockoutStatusEl.textContent = text;
  homeControlLockoutStatusEl.classList.toggle('error', isError);
}

function updateHomeControlLockoutStatus(nodes) {
  if (!Array.isArray(nodes) || nodes.length === 0) {
    setHomeControlLockoutStatus('Control battery lockout: unknown.', false);
    return;
  }

  const controlNode = nodes.find((node) => String(node && node.role ? node.role : '').toUpperCase() === 'CONTROL');
  if (!controlNode) {
    setHomeControlLockoutStatus('Control battery lockout: no control unit.', false);
    return;
  }

  const state = String(controlNode.state || '').toUpperCase();
  const lockout = String(controlNode.irrigationLockout || 'NONE').toUpperCase();

  if (lockout === 'LOW_BATTERY') {
    setHomeControlLockoutStatus('Control battery lockout: active (irrigation blocked).', true);
    return;
  }

  if (state === 'OFFLINE') {
    setHomeControlLockoutStatus('Control battery lockout: unknown (control offline).', true);
    return;
  }

  setHomeControlLockoutStatus('Control battery lockout: not active.', false);
}

function setUnitConfigStatus(text, isError = false) {
  if (!unitConfigStatusEl) {
    return;
  }
  unitConfigStatusEl.textContent = text;
  unitConfigStatusEl.classList.toggle('error', isError);
}

function setFactoryResetStatus(text, isError = false) {
  if (!factoryResetStatusEl) {
    return;
  }
  factoryResetStatusEl.textContent = text;
  factoryResetStatusEl.classList.toggle('error', isError);
}

function renderHomeModeControls() {
  if (!homeModeFormEl || !homeModeSaveBtnEl) {
    return;
  }

  const selectedMode = normalizeIrrigationMode(pendingIrrigationMode);
  const radios = homeModeFormEl.querySelectorAll('input[name="irrigationMode"]');
  radios.forEach((radio) => {
    radio.checked = radio.value === selectedMode;
    const wrapper = radio.closest('label');
    if (wrapper) {
      wrapper.classList.toggle('persisted', radio.value === normalizeIrrigationMode(persistedIrrigationMode));
    }
  });

  const isDirty = isHomeModeDirty();
  const inactiveLabel = selectedMode === 'OFF' ? 'Disabled' : 'Active';
  const actionLabel = selectedMode === 'OFF' ? 'Disable' : 'Activate';
  homeModeSaveBtnEl.disabled = !isDirty;
  homeModeSaveBtnEl.textContent = isDirty ? actionLabel : inactiveLabel;
  homeModeSaveBtnEl.classList.toggle('saved', !isDirty);

  const isManualMode = selectedMode === 'MANUAL';
  const isPersistedManual = normalizeIrrigationMode(persistedIrrigationMode) === 'MANUAL';
  const showManualActions = isManualMode && isPersistedManual;

  if (homeManualActionsEl) {
    homeManualActionsEl.hidden = !showManualActions;
  }

  if (manualStartBtnEl && manualStopBtnEl) {
    manualStartBtnEl.disabled = !showManualActions || isManualIrrigationActive;
    manualStopBtnEl.disabled = !showManualActions || !isManualIrrigationActive;
  }
}

function applyIrrigationConfig(config, allowOverridePending = true) {
  const mode = normalizeIrrigationMode(config && config.mode);
  persistedIrrigationMode = mode;
  if (config && typeof config.manualActive !== 'undefined') {
    isManualIrrigationActive = Boolean(config.manualActive);
  }
  if (allowOverridePending || !homeModeSaveBtnEl || homeModeSaveBtnEl.disabled) {
    pendingIrrigationMode = mode;
  }
  renderHomeModeControls();
  setHomeManualStatus(isManualIrrigationActive ? 'Manual irrigation active.' : 'Manual irrigation inactive.', false);
}

function renderUnitConfigControls() {
  if (unitNameSaveBtnEl) {
    const normalizedPending = String(pendingUnitName || '').trim();
    const normalizedPersisted = String(persistedUnitName || '').trim();
    const isDirty = normalizedPending.length > 0 && normalizedPending !== normalizedPersisted;
    unitNameSaveBtnEl.disabled = !isDirty;
    unitNameSaveBtnEl.textContent = isDirty ? 'Save name' : 'Saved';
  }

  if (unitPasswordSaveBtnEl && unitPasswordInputEl) {
    const value = String(unitPasswordInputEl.value || '').trim();
    const validLength = value.length >= 8 && value.length <= 63;
    unitPasswordSaveBtnEl.disabled = !validLength;
    unitPasswordSaveBtnEl.textContent = 'Set password';
  }

  if (factoryResetBtnEl && factoryResetConfirmInputEl) {
    const confirmText = String(factoryResetConfirmInputEl.value || '').trim().toUpperCase();
    factoryResetBtnEl.disabled = confirmText !== 'RESET';
  }
}

function applyUnitStatus(status, allowOverridePending = true) {
  if (!status) {
    return;
  }

  const apSsid = String(status.apSsid || '-');
  const firmwareVersion = String(status.firmwareVersion || '-');
  const uptimeSec = Number(status.uptimeSec);
  const unitName = String(status.unitName || '').trim();
  const passwordSet = Boolean(status.apPasswordSet);

  if (unitApSsidEl) {
    unitApSsidEl.textContent = apSsid;
  }
  if (unitFirmwareEl) {
    unitFirmwareEl.textContent = firmwareVersion;
  }
  if (unitUptimeEl) {
    unitUptimeEl.textContent = formatDuration(Number.isFinite(uptimeSec) ? uptimeSec : 0);
  }
  if (unitPasswordStateEl) {
    unitPasswordStateEl.textContent = passwordSet ? 'Set' : 'Open (no password)';
  }

  persistedUnitName = unitName;
  if (allowOverridePending || !unitNameSaveBtnEl || unitNameSaveBtnEl.disabled || !unitNameInitialized) {
    pendingUnitName = unitName;
    if (unitNameInputEl) {
      unitNameInputEl.value = unitName;
    }
    unitNameInitialized = true;
  }

  renderUnitConfigControls();
}

async function saveIrrigationConfig() {
  const mode = normalizeIrrigationMode(pendingIrrigationMode);
  try {
    await postForm('/api/irrigation/config', { mode });
    persistedIrrigationMode = mode;
    renderHomeModeControls();
    setHomeModeStatus('Config saved.', false);
  } catch (error) {
    setHomeModeStatus(`Save failed: ${error.message}`, true);
  }
}

async function saveUnitName() {
  const name = String(pendingUnitName || '').trim();
  if (!name) {
    setUnitConfigStatus('Unit name cannot be empty.', true);
    return;
  }

  try {
    await postForm('/api/unit/rename', { name });
    persistedUnitName = name;
    renderUnitConfigControls();
    setUnitConfigStatus('Name saved. Reconnect if AP restarts.', false);
  } catch (error) {
    setUnitConfigStatus(`Name save failed: ${error.message}`, true);
  }
}

async function saveUnitPassword() {
  if (!unitPasswordInputEl) {
    return;
  }

  const password = String(unitPasswordInputEl.value || '').trim();
  if (password.length < 8 || password.length > 63) {
    setUnitConfigStatus('Password must be 8-63 characters.', true);
    return;
  }

  try {
    await postForm('/api/unit/password', { password });
    unitPasswordInputEl.value = '';
    renderUnitConfigControls();
    setUnitConfigStatus('Password saved. Reconnect if AP restarts.', false);
  } catch (error) {
    setUnitConfigStatus(`Password save failed: ${error.message}`, true);
  }
}

async function runFactoryReset() {
  if (!factoryResetConfirmInputEl) {
    return;
  }

  const confirmText = String(factoryResetConfirmInputEl.value || '').trim().toUpperCase();
  if (confirmText !== 'RESET') {
    setFactoryResetStatus('Type RESET to enable factory reset.', true);
    renderUnitConfigControls();
    return;
  }

  const confirmed = window.confirm('Factory reset will clear pairing and restore AP defaults. Continue?');
  if (!confirmed) {
    return;
  }

  try {
    await postForm('/api/unit/factory-reset', { confirm: 'RESET' });
    factoryResetConfirmInputEl.value = '';
    renderUnitConfigControls();
    setFactoryResetStatus('Factory reset sent. Device may reboot/restart AP.', false);
    setUnitConfigStatus('Factory reset applied. Reconnect to default AP if needed.', false);
    await tick();
  } catch (error) {
    setFactoryResetStatus(`Factory reset failed: ${error.message}`, true);
  }
}

function setActiveTab(tabName) {
  tabButtons.forEach((button) => {
    button.classList.toggle('active', button.dataset.tab === tabName);
  });
  tabPanels.forEach((panel) => {
    panel.classList.toggle('active', panel.dataset.panel === tabName);
  });
}

function renderHomeSummary(summary) {
  if (!summary) {
    if (homeOnlineEl) homeOnlineEl.textContent = '-';
    if (homeMoistureEl) homeMoistureEl.textContent = '-';
    if (homePairingEl) homePairingEl.textContent = '-';
    if (homeUptimeEl) homeUptimeEl.textContent = '-';
    return;
  }

  const online = Number.isFinite(Number(summary.onlineSensors)) ? Number(summary.onlineSensors) : 0;
  const total = Number.isFinite(Number(summary.totalVisibleSensors)) ? Number(summary.totalVisibleSensors) : 0;
  const avgMoisturePermille = summary.avgMoisturePermille;
  const pairingOpen = Boolean(summary.pairingOpen);
  const pairingSec = Number(summary.pairingRemainingSec);
  const uptimeSec = Number(summary.uptimeSec);

  if (homeOnlineEl) {
    homeOnlineEl.textContent = `${online}/${total}`;
  }
  if (homeMoistureEl) {
    homeMoistureEl.textContent = Number.isFinite(Number(avgMoisturePermille))
      ? formatMoisture(Number(avgMoisturePermille))
      : 'N/A';
  }
  if (homePairingEl) {
    homePairingEl.textContent = pairingOpen
      ? `Open (${Math.max(0, Math.floor(Number.isFinite(pairingSec) ? pairingSec : 0))}s)`
      : 'Closed';
  }
  if (homeUptimeEl) {
    homeUptimeEl.textContent = formatDuration(Number.isFinite(uptimeSec) ? uptimeSec : 0);
  }
}

async function postForm(url, payload) {
  const body = new URLSearchParams();
  Object.entries(payload || {}).forEach(([key, value]) => {
    body.set(key, String(value));
  });

  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: body.toString(),
  });
  if (!response.ok) {
    const error = new Error(`${url} -> HTTP ${response.status}`);
    error.status = response.status;
    throw error;
  }
}

async function renameSensor(nodeId) {
  const nextName = window.prompt(`Rename unit ${nodeId}:`);
  if (!nextName) {
    return;
  }

  try {
    await postForm('/api/sensors/rename', { nodeId, name: nextName.trim() });
    await tick();
  } catch (error) {
    if (error.status === 404) {
      window.alert('Rename endpoint is not implemented on firmware yet.');
      return;
    }
    window.alert(`Rename failed: ${error.message}`);
  }
}

async function unpairSensor(nodeId) {
  const ok = window.confirm(`Unpair unit ${nodeId}?`);
  if (!ok) {
    return;
  }

  try {
    await postForm('/api/sensors/unpair', { nodeId });
    await tick();
  } catch (error) {
    if (error.status === 404) {
      window.alert('Unpair endpoint is not implemented on firmware yet.');
      return;
    }
    window.alert(`Unpair failed: ${error.message}`);
  }
}

async function openPairingWindow() {
  try {
    await postForm('/api/pairing/open', {});
    await tick();
  } catch (error) {
    window.alert(`Open pairing failed: ${error.message}`);
  }
}

async function closePairingWindow() {
  try {
    await postForm('/api/pairing/close', {});
    await tick();
  } catch (error) {
    window.alert(`Close pairing failed: ${error.message}`);
  }
}

async function startManualIrrigation() {
  try {
    await postForm('/api/irrigation/manual/start', {});
    isManualIrrigationActive = true;
    renderHomeModeControls();
    setHomeManualStatus('Manual irrigation started.', false);
    await tick();
  } catch (error) {
    setHomeManualStatus(`Manual start failed: ${error.message}`, true);
  }
}

async function stopManualIrrigation() {
  try {
    await postForm('/api/irrigation/manual/stop', {});
    isManualIrrigationActive = false;
    renderHomeModeControls();
    setHomeManualStatus('Manual irrigation stopped.', false);
    await tick();
  } catch (error) {
    setHomeManualStatus(`Manual stop failed: ${error.message}`, true);
  }
}

async function triggerRemoteCalibration(nodeId, step) {
  await postForm('/api/sensors/calibrate', { nodeId, step });
}

function calibrationStateFor(nodeId) {
  return calibrationStateByNode.get(String(nodeId)) || 'idle';
}

function resetCalibrationState(nodeId) {
  calibrationStateByNode.delete(String(nodeId));
}

function escapeHtml(value) {
  return String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function renderCalibrationBanner() {
  if (!calibrationBannerEl) {
    return;
  }

  if (!activeCalibration) {
    calibrationBannerEl.hidden = true;
    calibrationBannerEl.classList.remove('error');
    calibrationBannerEl.innerHTML = '';
    return;
  }

  const now = Date.now();
  let noteText = activeCalibration.note || '';
  if (activeCalibration.phase === 'measure_wet' && activeCalibration.deadlineMs > now) {
    const leftSec = Math.max(0, Math.ceil((activeCalibration.deadlineMs - now) / 1000));
    noteText = `${noteText} (${leftSec}s left)`;
  }

  const title = activeCalibration.title || `Sensor ${activeCalibration.nodeId}`;
  const stepsHtml = activeCalibration.steps
    .map((step) => `<li class="calibration-step ${escapeHtml(step.state)}">${escapeHtml(step.text)}</li>`)
    .join('');
  const noteHtml = noteText ? `<p class="calibration-note">${escapeHtml(noteText)}</p>` : '';

  calibrationBannerEl.classList.toggle('error', activeCalibration.status === 'error');
  calibrationBannerEl.innerHTML = `
    <p class="calibration-title">Calibration · ${escapeHtml(title)}</p>
    <ul class="calibration-steps">${stepsHtml}</ul>
    ${noteHtml}
  `;
  calibrationBannerEl.hidden = false;
}

function placeCalibrationBanner() {
  if (!calibrationBannerEl) {
    return;
  }

  if (!activeCalibration) {
    return;
  }

  const targetCard = nodesEl.querySelector(`[data-sensor-node-id="${activeCalibration.nodeId}"]`);
  if (!targetCard) {
    return;
  }

  const actions = targetCard.querySelector('.sensor-actions');
  if (actions) {
    actions.insertAdjacentElement('afterend', calibrationBannerEl);
    return;
  }

  targetCard.appendChild(calibrationBannerEl);
}

function finishCalibrationWithError(message) {
  if (!activeCalibration) {
    return;
  }

  const nodeId = activeCalibration.nodeId;
  resetCalibrationState(nodeId);
  activeCalibration.status = 'error';
  activeCalibration.phase = 'done';
  activeCalibration.deadlineMs = 0;
  activeCalibration.autoHideAtMs = Date.now() + CAL_ERROR_HIDE_MS;
  activeCalibration.note = message;
  activeCalibration.steps = activeCalibration.steps.map((step) => ({
    ...step,
    state: step.state === 'done' ? 'done' : 'pending',
  }));
  renderCalibrationBanner();
}

function startCalibrationGuide(node) {
  const nodeId = String(node.nodeId);
  const title = node.name || `Sensor ${nodeId}`;
  calibrationStateByNode.clear();
  calibrationStateByNode.set(nodeId, 'measure_wet');

  activeCalibration = {
    nodeId,
    title,
    status: 'info',
    phase: 'measure_wet',
    deadlineMs: Date.now() + CAL_PROMPT_TIMEOUT_MS,
    autoHideAtMs: 0,
    steps: [
      { text: 'Calibrate command sent', state: 'done' },
      { text: 'Measure wet', state: 'active' },
      { text: 'Sensor finishes calibration', state: 'pending' },
    ],
    note: 'Dry measurement completed. Place sensor in water and tap "Measure wet".',
  };

  renderCalibrationBanner();
}

function finishCalibrationGuide(node) {
  const nodeId = String(node.nodeId);
  resetCalibrationState(nodeId);

  activeCalibration = {
    nodeId,
    title: node.name || `Sensor ${nodeId}`,
    status: 'info',
    phase: 'done',
    deadlineMs: 0,
    autoHideAtMs: Date.now() + CAL_INFO_HIDE_MS,
    steps: [
      { text: 'Calibrate command sent', state: 'done' },
      { text: 'Measure wet', state: 'done' },
      { text: 'Sensor finishes calibration', state: 'active' },
    ],
    note: 'Wet measurement command sent. Sensor now completes calibration.',
  };

  renderCalibrationBanner();
}

function moveCalibrationToSensorFinishing(node) {
  const nodeId = String(node.nodeId);
  const baselineRxPackets = Number.isFinite(Number(node.rxPackets)) ? Number(node.rxPackets) : 0;
  calibrationStateByNode.set(nodeId, 'sensor_finishing');

  activeCalibration = {
    nodeId,
    title: node.name || `Sensor ${nodeId}`,
    status: 'info',
    phase: 'sensor_finishing',
    deadlineMs: 0,
    autoHideAtMs: 0,
    baselineRxPackets,
    steps: [
      { text: 'Calibrate command sent', state: 'done' },
      { text: 'Measure wet', state: 'done' },
      { text: 'Sensor finishes calibration', state: 'active' },
    ],
    note: 'Wet command sent. Waiting for sensor telemetry update.',
  };

  renderCalibrationBanner();
}

function reconcileCalibrationState() {
  if (!activeCalibration) {
    return;
  }

  const now = Date.now();
  const nodeExists = latestNodes.some((node) => String(node.nodeId) === activeCalibration.nodeId);
  if (!nodeExists) {
    finishCalibrationWithError('Calibration canceled: sensor is no longer visible.');
    return;
  }

  if (activeCalibration.phase === 'measure_wet' && activeCalibration.deadlineMs > 0 && now >= activeCalibration.deadlineMs) {
    finishCalibrationWithError('Wet-step timeout. State returned to Calibrate.');
    return;
  }

  if (activeCalibration.phase === 'sensor_finishing') {
    const currentNode = latestNodes.find((node) => String(node.nodeId) === activeCalibration.nodeId);
    if (!currentNode) {
      finishCalibrationWithError('Calibration canceled: sensor is no longer visible.');
      return;
    }

    if (currentNode.state === 'OFFLINE') {
      finishCalibrationWithError('Calibration failed: sensor went offline.');
      return;
    }

    const currentRxPackets = Number.isFinite(Number(currentNode.rxPackets)) ? Number(currentNode.rxPackets) : 0;
    if (currentRxPackets > Number(activeCalibration.baselineRxPackets || 0)) {
      finishCalibrationGuide(currentNode);
      return;
    }
  }

  if (activeCalibration.autoHideAtMs > 0 && now >= activeCalibration.autoHideAtMs) {
    activeCalibration = null;
    renderCalibrationBanner();
  }
}

function renderPairingBannerState(isOpen) {
  if (!pairingBannerEl) {
    return;
  }

  if (addSensorBtn) {
    addSensorBtn.hidden = isOpen;
  }
  if (closePairingBtn) {
    closePairingBtn.hidden = !isOpen;
  }

  pairingBannerEl.textContent = isOpen
    ? `Pairing window is open (${Math.max(0, pairingRemainingSec)}s left).`
    : 'Pairing window is open (120s).';
  pairingBannerEl.hidden = !isOpen;
}

function syncPairingCountdown(webStatus) {
  const isOpen = Boolean(webStatus && webStatus.pairingOpen);
  const serverRemainingSec = Number(webStatus && webStatus.pairingRemainingSec);
  pairingRemainingSec = isOpen && Number.isFinite(serverRemainingSec)
    ? Math.max(0, Math.floor(serverRemainingSec))
    : 0;
  renderPairingBannerState(isOpen);
}

function renderNodes(nodes) {
  if (!Array.isArray(nodes) || nodes.length === 0) {
    nodesEl.innerHTML = '<p class="empty">No unit data yet.</p>';
    return;
  }

  const sortedNodes = [...nodes].sort((left, right) => {
    const leftRole = String(left && left.role ? left.role : 'SENSOR').toUpperCase();
    const rightRole = String(right && right.role ? right.role : 'SENSOR').toUpperCase();
    const leftControl = leftRole === 'CONTROL';
    const rightControl = rightRole === 'CONTROL';
    if (leftControl !== rightControl) {
      return leftControl ? -1 : 1;
    }

    const leftId = Number(left && left.nodeId);
    const rightId = Number(right && right.nodeId);
    const leftHasId = Number.isFinite(leftId);
    const rightHasId = Number.isFinite(rightId);
    if (leftHasId && rightHasId && leftId !== rightId) {
      return leftId - rightId;
    }
    if (leftHasId !== rightHasId) {
      return leftHasId ? -1 : 1;
    }
    return 0;
  });

  const html = sortedNodes
    .map((node) => {
      const role = String(node.role || 'SENSOR').toUpperCase();
      const isControl = role === 'CONTROL';
      const roleBadge = role === 'CONTROL' ? 'CONTROL' : role === 'SENSOR' ? 'SENSOR' : 'UNKNOWN';
      const batteryState = node.batteryState || 'UNKNOWN';
      const cardClasses = ['sensor-card'];
      if (isControl) {
        cardClasses.push('control');
      }
      if (batteryState === 'NEEDS_REPLACEMENT') {
        cardClasses.push('needs-replacement');
      }
      if (node.state === 'OFFLINE') {
        cardClasses.push('offline');
      }
      const batteryClass =
        batteryState === 'CRITICAL'
          ? 'battery-state critical'
          : batteryState === 'NEEDS_REPLACEMENT'
            ? 'battery-state needs-replacement'
            : 'battery-state';
      const calibrationState = calibrationStateFor(node.nodeId);
      const calibrateLabel = calibrationState === 'measure_wet'
        ? 'Measure wet'
        : calibrationState === 'sensor_finishing'
          ? 'Calibrating...'
          : 'Calibrate';
      const calibrateDisabled =
        (activeCalibration && String(activeCalibration.nodeId) !== String(node.nodeId)) ||
        calibrationState === 'sensor_finishing'
          ? 'disabled'
          : '';
      const defaultTitle = isControl
        ? `Control ${node.nodeId ?? '-'}`
        : `Sensor ${node.nodeId ?? '-'}`;
      const irrigationLockout = String(node.irrigationLockout || 'NONE').toUpperCase();
      const moistureLine = isControl
        ? ''
        : `<p>Moisture: ${formatMoisture(node.moisturePermille)}</p>`;
      const irrigationLine = isControl
        ? (irrigationLockout === 'LOW_BATTERY'
          ? '<p>Irrigation: <span class="battery-state critical">Blocked (low battery)</span></p>'
          : '<p>Irrigation: Allowed</p>')
        : '';
      const calibrateButton = isControl
        ? ''
        : `<button type="button" class="sensor-btn calibrate" data-action="calibrate" data-node-id="${node.nodeId ?? ''}" ${calibrateDisabled}>${calibrateLabel}</button>`;

      return `
        <article class="${cardClasses.join(' ')}" data-sensor-node-id="${node.nodeId ?? ''}">
          <h3>${node.name || defaultTitle}</h3>
          <p>Role: ${roleBadge}</p>
          ${moistureLine}
          ${irrigationLine}
          <p>State: ${node.state ?? 'UNKNOWN'}</p>
          <p>Battery: ${formatBatteryVolts(node.batteryEstMv)} <span class="${batteryClass}">[${batteryState}]</span></p>
          <p>Last seen: ${node.lastSeenSecAgo ?? '-'} sec ago</p>
          <div class="sensor-actions">
            <button type="button" class="sensor-btn rename" data-action="rename" data-node-id="${node.nodeId ?? ''}">Rename</button>
            ${calibrateButton}
            <button type="button" class="sensor-btn unpair" data-action="unpair" data-node-id="${node.nodeId ?? ''}">Unpair</button>
          </div>
          <p class="sensor-mac">MAC: ${node.mac ?? 'N/A'}</p>
        </article>
      `;
    })
    .join('');

  nodesEl.innerHTML = html;
}

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`${url} -> HTTP ${response.status}`);
  }
  return response.json();
}

async function tick() {
  try {
    const [webStatus, nodes, summary, irrigationConfig, unitStatus] = await Promise.all([
      fetchJson('/api/web/status'),
      fetchJson('/api/nodes'),
      fetchJson('/api/system/summary'),
      fetchJson('/api/irrigation/config'),
      fetchJson('/api/unit/status'),
    ]);
    latestNodes = Array.isArray(nodes) ? nodes : [];

    render(webStatusEl, webStatus);
    renderHomeSummary(summary);
    applyIrrigationConfig(irrigationConfig, !homeModeSaveBtnEl || homeModeSaveBtnEl.disabled);
    if (summary && typeof summary.manualIrrigationActive !== 'undefined') {
      isManualIrrigationActive = Boolean(summary.manualIrrigationActive);
      renderHomeModeControls();
      setHomeManualStatus(isManualIrrigationActive ? 'Manual irrigation active.' : 'Manual irrigation inactive.', false);
    }
    applyUnitStatus(unitStatus, !unitNameSaveBtnEl || unitNameSaveBtnEl.disabled);
    updateHomeControlLockoutStatus(latestNodes);
    syncPairingCountdown(webStatus);
    renderNodes(nodes);
    reconcileCalibrationState();
    renderCalibrationBanner();
    placeCalibrationBanner();
  } catch (error) {
    if (webStatusEl) {
      webStatusEl.textContent = `fetch error: ${error}`;
    }
    nodesEl.textContent = '';
    latestNodes = [];
    renderHomeSummary(null);
    if (pairingBannerEl) {
      pairingRemainingSec = 0;
      pairingBannerEl.hidden = true;
    }
    setHomeModeStatus(`Config fetch error: ${error}`, true);
    setHomeManualStatus(`Manual control unavailable: ${error}`, true);
    setHomeControlLockoutStatus(`Control battery lockout unavailable: ${error}`, true);
    activeCalibration = null;
    calibrationStateByNode.clear();
    renderCalibrationBanner();
  }
}

if (homeModeFormEl) {
  homeModeFormEl.addEventListener('change', (event) => {
    const target = event.target;
    if (!(target instanceof HTMLInputElement)) {
      return;
    }
    if (target.name !== 'irrigationMode') {
      return;
    }

    pendingIrrigationMode = normalizeIrrigationMode(target.value);
    renderHomeModeControls();
    setHomeModeStatus(isHomeModeDirty() ? 'Unsaved changes.' : 'Config synced.', false);
  });
}

if (homeModeSaveBtnEl) {
  homeModeSaveBtnEl.addEventListener('click', async () => {
    await saveIrrigationConfig();
  });
}

if (manualStartBtnEl) {
  manualStartBtnEl.addEventListener('click', async () => {
    await startManualIrrigation();
  });
}

if (manualStopBtnEl) {
  manualStopBtnEl.addEventListener('click', async () => {
    await stopManualIrrigation();
  });
}

if (unitNameInputEl) {
  unitNameInputEl.addEventListener('input', () => {
    pendingUnitName = String(unitNameInputEl.value || '').trim();
    renderUnitConfigControls();
    setUnitConfigStatus('Unsaved name change.', false);
  });
}

if (unitPasswordInputEl) {
  unitPasswordInputEl.addEventListener('input', () => {
    renderUnitConfigControls();
    if (String(unitPasswordInputEl.value || '').trim().length > 0) {
      setUnitConfigStatus('Unsaved password change.', false);
    }
  });
}

if (factoryResetConfirmInputEl) {
  factoryResetConfirmInputEl.addEventListener('input', () => {
    renderUnitConfigControls();
    if (String(factoryResetConfirmInputEl.value || '').trim().length > 0) {
      setFactoryResetStatus('Confirmation text entered.', false);
    } else {
      setFactoryResetStatus('No pending action.', false);
    }
  });
}

if (unitNameSaveBtnEl) {
  unitNameSaveBtnEl.addEventListener('click', async () => {
    await saveUnitName();
  });
}

if (unitPasswordSaveBtnEl) {
  unitPasswordSaveBtnEl.addEventListener('click', async () => {
    await saveUnitPassword();
  });
}

if (factoryResetBtnEl) {
  factoryResetBtnEl.addEventListener('click', async () => {
    await runFactoryReset();
  });
}

if (tabsEl) {
  tabsEl.addEventListener('click', (event) => {
    const target = event.target;
    if (!(target instanceof HTMLElement)) {
      return;
    }

    const tabName = target.dataset.tab;
    if (!tabName) {
      return;
    }
    setActiveTab(tabName);
  });
}

nodesEl.addEventListener('click', async (event) => {
  const target = event.target;
  if (!(target instanceof HTMLElement)) {
    return;
  }

  const action = target.dataset.action;
  const nodeId = target.dataset.nodeId;
  if (!action || !nodeId) {
    return;
  }

  if (action === 'rename') {
    await renameSensor(nodeId);
    return;
  }

  if (action === 'calibrate') {
    const node = latestNodes.find((item) => String(item.nodeId) === String(nodeId));
    if (!node) {
      return;
    }

    if (activeCalibration && String(activeCalibration.nodeId) !== String(nodeId)) {
      window.alert('Calibration is already active on another sensor.');
      return;
    }

    try {
      if (calibrationStateFor(nodeId) === 'measure_wet') {
        await triggerRemoteCalibration(nodeId, 'wet');
        moveCalibrationToSensorFinishing(node);
      } else {
        await triggerRemoteCalibration(nodeId, 'start');
        startCalibrationGuide(node);
      }
    } catch (error) {
      finishCalibrationWithError(`Calibration command failed: ${error.message}`);
      renderNodes(latestNodes);
      return;
    }

    renderNodes(latestNodes);
    renderCalibrationBanner();
    placeCalibrationBanner();
    return;
  }

  if (action === 'unpair') {
    if (activeCalibration && activeCalibration.nodeId === String(nodeId)) {
      finishCalibrationWithError('Calibration canceled: sensor was unpaired.');
    }
    resetCalibrationState(nodeId);
    await unpairSensor(nodeId);
  }
});

setInterval(tick, 3000);
tick();

setInterval(() => {
  if (pairingRemainingSec > 0) {
    pairingRemainingSec -= 1;
    renderPairingBannerState(pairingRemainingSec > 0);
  }
  reconcileCalibrationState();
  renderCalibrationBanner();
  placeCalibrationBanner();
}, 1000);

if (addSensorBtn) {
  addSensorBtn.addEventListener('click', async () => {
    await openPairingWindow();
  });
}

if (closePairingBtn) {
  closePairingBtn.addEventListener('click', async () => {
    await closePairingWindow();
  });
}
