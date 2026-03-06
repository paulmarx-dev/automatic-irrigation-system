const webStatusEl = document.getElementById('webStatus');
const nodesEl = document.getElementById('nodes');

function render(el, data) {
  el.textContent = JSON.stringify(data, null, 2);
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
    render(nodesEl, nodes);
  } catch (error) {
    webStatusEl.textContent = `fetch error: ${error}`;
  }
}

setInterval(tick, 3000);
tick();
