const webStatusEl = document.getElementById('webStatus');
const nodesEl = document.getElementById('nodes');
const addSensorBtn = document.getElementById('addSensorBtn');
const pairingBannerEl = document.getElementById('pairingBanner');
let pairingRemainingSec = 0;

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

      return `
        <article class="${cardClasses.join(' ')}">
          <h3>${node.name || `Sensor ${node.nodeId ?? '-'}`}</h3>
          <p>Moisture: ${formatMoisture(node.moisturePermille)}</p>
          <p>State: ${node.state ?? 'UNKNOWN'}</p>
          <p>Battery: ${formatBatteryVolts(node.batteryEstMv)} <span class="${batteryClass}">[${batteryState}]</span></p>
          <p>Last seen: ${node.lastSeenSecAgo ?? '-'} sec ago</p>
          <div class="sensor-actions">
            <button type="button" class="sensor-btn rename" data-action="rename" data-node-id="${node.nodeId ?? ''}">Rename</button>
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
    const [webStatus, nodes] = await Promise.all([
      fetchJson('/api/web/status'),
      fetchJson('/api/nodes'),
    ]);

    render(webStatusEl, webStatus);
    syncPairingCountdown(webStatus);
    renderNodes(nodes);
  } catch (error) {
    webStatusEl.textContent = `fetch error: ${error}`;
    nodesEl.textContent = '';
    if (pairingBannerEl) {
      pairingRemainingSec = 0;
      pairingBannerEl.hidden = true;
    }
  }
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

  if (action === 'unpair') {
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
}, 1000);

if (addSensorBtn) {
  addSensorBtn.addEventListener('click', async () => {
    await openPairingWindow();
  });
}
