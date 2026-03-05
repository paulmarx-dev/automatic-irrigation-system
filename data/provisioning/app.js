const statusEl = document.getElementById('status');
const form = document.getElementById('cfgForm');
const resetBtn = document.getElementById('resetBtn');

function renderStatus(data) {
  statusEl.textContent = JSON.stringify(data, null, 2);
}

async function fetchStatus() {
  try {
    const response = await fetch('/api/provisioning/status');
    const data = await response.json();
    renderStatus(data);
  } catch (error) {
    statusEl.textContent = `status error: ${error}`;
  }
}

form.addEventListener('submit', async (event) => {
  event.preventDefault();

  const body = new URLSearchParams();
  body.set('ssid', document.getElementById('ssid').value.trim());
  body.set('password', document.getElementById('password').value);

  try {
    const response = await fetch('/api/provisioning/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body,
    });
    const data = await response.json();
    renderStatus(data);
  } catch (error) {
    statusEl.textContent = `submit error: ${error}`;
  }

  setTimeout(fetchStatus, 500);
});

resetBtn.addEventListener('click', async () => {
  try {
    const response = await fetch('/api/provisioning/reset', { method: 'POST' });
    const data = await response.json();
    renderStatus(data);
  } catch (error) {
    statusEl.textContent = `reset error: ${error}`;
  }

  setTimeout(fetchStatus, 500);
});

setInterval(fetchStatus, 2000);
fetchStatus();
