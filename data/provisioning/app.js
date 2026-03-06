const webStatusEl = document.getElementById('webStatus');
const nodesEl = document.getElementById('nodes');
const addSensorBtn = document.getElementById('addSensorBtn');
const pairingBannerEl = document.getElementById('pairingBanner');
const calibrationBannerEl = document.getElementById('calibrationBanner');
const tabsEl = document.querySelector('.tabs');
const tabButtons = Array.from(document.querySelectorAll('.tab-btn'));
const tabPanels = Array.from(document.querySelectorAll('.tab-panel'));
const homeOnlineEl = document.getElementById('homeOnline');
const homeMoistureEl = document.getElementById('homeMoisture');
const homePairingEl = document.getElementById('homePairing');
const homeUptimeEl = document.getElementById('homeUptime');
const calibrationStateByNode = new Map();
let latestNodes = [];
let activeCalibration = null;
let pairingRemainingSec = 0;
const CAL_PROMPT_TIMEOUT_MS = 20000;
const CAL_ERROR_HIDE_MS = 5000;
const CAL_INFO_HIDE_MS = 5000;

function render(el, data) {
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
  const nextName = window.prompt(`Rename sensor ${nodeId}:`);
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
  const ok = window.confirm(`Unpair sensor ${nodeId}?`);
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

  if (activeCalibration.autoHideAtMs > 0 && now >= activeCalibration.autoHideAtMs) {
    activeCalibration = null;
    renderCalibrationBanner();
  }
}

function renderPairingBannerState(isOpen) {
  if (!pairingBannerEl) {
    return;
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
    nodesEl.innerHTML = '<p class="empty">No sensor data yet.</p>';
    return;
  }

  const html = nodes
    .map((node) => {
      const batteryState = node.batteryState || 'UNKNOWN';
      const cardClasses = ['sensor-card'];
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
        : 'Calibrate';
      const calibrateDisabled = '';

      return `
        <article class="${cardClasses.join(' ')}" data-sensor-node-id="${node.nodeId ?? ''}">
          <h3>${node.name || `Sensor ${node.nodeId ?? '-'}`}</h3>
          <p>Moisture: ${formatMoisture(node.moisturePermille)}</p>
          <p>State: ${node.state ?? 'UNKNOWN'}</p>
          <p>Battery: ${formatBatteryVolts(node.batteryEstMv)} <span class="${batteryClass}">[${batteryState}]</span></p>
          <p>Last seen: ${node.lastSeenSecAgo ?? '-'} sec ago</p>
          <div class="sensor-actions">
            <button type="button" class="sensor-btn rename" data-action="rename" data-node-id="${node.nodeId ?? ''}">Rename</button>
            <button type="button" class="sensor-btn calibrate" data-action="calibrate" data-node-id="${node.nodeId ?? ''}" ${calibrateDisabled}>${calibrateLabel}</button>
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
    const [webStatus, nodes, summary] = await Promise.all([
      fetchJson('/api/web/status'),
      fetchJson('/api/nodes'),
      fetchJson('/api/system/summary'),
    ]);
    latestNodes = Array.isArray(nodes) ? nodes : [];

    render(webStatusEl, webStatus);
    renderHomeSummary(summary);
    syncPairingCountdown(webStatus);
    renderNodes(nodes);
    reconcileCalibrationState();
    renderCalibrationBanner();
    placeCalibrationBanner();
  } catch (error) {
    webStatusEl.textContent = `fetch error: ${error}`;
    nodesEl.textContent = '';
    latestNodes = [];
    renderHomeSummary(null);
    if (pairingBannerEl) {
      pairingRemainingSec = 0;
      pairingBannerEl.hidden = true;
    }
    activeCalibration = null;
    calibrationStateByNode.clear();
    renderCalibrationBanner();
  }
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

    try {
      if (calibrationStateFor(nodeId) === 'measure_wet') {
        await triggerRemoteCalibration(nodeId, 'wet');
        finishCalibrationGuide(node);
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
