const webStatusEl = document.getElementById('webStatus');
const nodesEl = document.getElementById('nodes');

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
          <h3>Sensor ${node.nodeId ?? '-'}</h3>
          <p>Moisture: ${formatMoisture(node.moisturePermille)}</p>
          <p>State: ${node.state ?? 'UNKNOWN'}</p>
          <p>Battery: ${formatBatteryVolts(node.batteryEstMv)} <span class="${batteryClass}">[${batteryState}]</span></p>
          <p>Last seen: ${node.lastSeenSecAgo ?? '-'} sec ago</p>
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
    renderNodes(nodes);
  } catch (error) {
    webStatusEl.textContent = `fetch error: ${error}`;
    nodesEl.textContent = '';
  }
}

setInterval(tick, 3000);
tick();
