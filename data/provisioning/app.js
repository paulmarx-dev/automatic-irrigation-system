const webStatusEl = document.getElementById('webStatus');
const nodesEl = document.getElementById('nodes');
const addSensorBtn = document.getElementById('addSensorBtn');
const closePairingBtn = document.getElementById('closePairingBtn');
const pairingBannerEl = document.getElementById('pairingBanner');
const calibrationBannerEl = document.getElementById('calibrationBanner');
const transportIndicatorBtnEl = document.getElementById('transportIndicatorBtn');
const transportPopoverEl = document.getElementById('transportPopover');
const transportPopoverTransportEl = document.getElementById('transportPopoverTransport');
const transportPopoverStatusEl = document.getElementById('transportPopoverStatus');
const transportPopoverLastUpdateEl = document.getElementById('transportPopoverLastUpdate');
const transportPopoverPollIntervalEl = document.getElementById('transportPopoverPollInterval');
const transportPopoverReconnectsEl = document.getElementById('transportPopoverReconnects');
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
const homeAutoConfigEl = document.getElementById('homeAutoConfig');
const homeManualConfigEl = document.getElementById('homeManualConfig');
const homeTimeConfigEl = document.getElementById('homeTimeConfig');
const manualStartBtnEl = document.getElementById('manualStartBtn');
const manualStopBtnEl = document.getElementById('manualStopBtn');
const manualDurationSecInputEl = document.getElementById('manualDurationSecInput');
const manualDurationHintEl = document.getElementById('manualDurationHint');
const autoStartMoisturePctInputEl = document.getElementById('autoStartMoisturePctInput');
const autoStopMoisturePctInputEl = document.getElementById('autoStopMoisturePctInput');
const autoStatusHintEl = document.getElementById('autoStatusHint');
const timeIntervalMinInputEl = document.getElementById('timeIntervalMinInput');
const timeRunDurationMinInputEl = document.getElementById('timeRunDurationMinInput');
const timeRunDurationHintEl = document.getElementById('timeRunDurationHint');
const timeIntervalHintEl = document.getElementById('timeIntervalHint');
const timeScheduleHintEl = document.getElementById('timeScheduleHint');
const homeManualStatusEl = document.getElementById('homeManualStatus');
const homeControlLockoutStatusEl = document.getElementById('homeControlLockoutStatus');
const trackExportCsvBtnEl = document.getElementById('trackExportCsvBtn');
const trackExportStatusEl = document.getElementById('trackExportStatus');
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
let persistedManualDurationSec = 120;
let pendingManualDurationSec = 120;
let persistedAutoStartPermille = 350;
let pendingAutoStartPermille = 350;
let persistedAutoStopPermille = 450;
let pendingAutoStopPermille = 450;
let latestAvgMoisturePermille = null;
let persistedTimeIntervalMin = 360;
let pendingTimeIntervalMin = 360;
let persistedTimeRunDurationSec = 300;
let pendingTimeRunDurationSec = 300;
let isManualIrrigationActive = false;
let isControlLowBatteryLockoutActive = false;
let controlAvailabilityStatus = 'not_paired';
let manualStartBlockedReason = 'control_not_paired';
let manualRunRemainingSec = 0;
let controlConfirmedState = 'idle';
let controlPendingElapsedSec = 0;
let localManualPendingCommand = 'none';
let localManualPendingSinceMs = 0;
let timeNextStartRemainingSec = 0;
let timeRunRemainingSec = 0;
let homeManualStatusResetTimer = 0;
let manualDurationDirty = false;
let persistedUnitName = '';
let pendingUnitName = '';
let unitNameInitialized = false;
let irrigationEventsSource = null;
let irrigationEventsReconnectTimer = 0;
let tickIntervalId = 0;
let activeTickIntervalMs = 0;
let transportStatus = 'reconnect';
let transportLastUpdateMs = 0;
let transportReconnectAttempts = 0;
const MANUAL_DURATION_MIN_SEC = 0;
const MANUAL_DURATION_MAX_SEC = 600;
const TIME_INTERVAL_MIN = 5;
const TIME_INTERVAL_MAX = 1440;
const TIME_RUN_MIN_SEC = 0;
const TIME_RUN_MAX_SEC = 600;
const CAL_PROMPT_TIMEOUT_MS = 20000;
const CAL_ERROR_HIDE_MS = 5000;
const CAL_INFO_HIDE_MS = 5000;
const MANUAL_PENDING_GUARD_SEC = 8;
const POLL_INTERVAL_FAST_MS = 3000;
const COUNTDOWN_RESYNC_THRESHOLD_SEC = 3;
let latestSummaryUptimeSec = null;

function transportStatusLabel(status) {
  if (status === 'live') {
    return 'Live SSE';
  }
  if (status === 'fallback') {
    return 'Fallback polling';
  }
  if (status === 'reconnect') {
    return 'Reconnecting';
  }
  return 'Offline/error';
}

function transportTypeLabel(status) {
  if (status === 'live' || status === 'reconnect') {
    return 'SSE';
  }
  if (status === 'fallback') {
    return 'Polling fallback';
  }
  return 'Unavailable';
}

function formatLastUpdateAge() {
  if (!transportLastUpdateMs) {
    return '-';
  }
  const ageSec = Math.max(0, Math.floor((Date.now() - transportLastUpdateMs) / 1000));
  return `${ageSec}s ago`;
}

function updateTransportPopover() {
  if (transportPopoverTransportEl) {
    transportPopoverTransportEl.textContent = transportTypeLabel(transportStatus);
  }
  if (transportPopoverStatusEl) {
    transportPopoverStatusEl.textContent = transportStatusLabel(transportStatus);
  }
  if (transportPopoverLastUpdateEl) {
    transportPopoverLastUpdateEl.textContent = formatLastUpdateAge();
  }
  if (transportPopoverPollIntervalEl) {
    transportPopoverPollIntervalEl.textContent = activeTickIntervalMs > 0 ? `${Math.floor(activeTickIntervalMs / 1000)}s` : '-';
  }
  if (transportPopoverReconnectsEl) {
    transportPopoverReconnectsEl.textContent = String(transportReconnectAttempts);
  }
}

function setTransportStatus(nextStatus) {
  const allowed = nextStatus === 'live'
    || nextStatus === 'fallback'
    || nextStatus === 'reconnect'
    || nextStatus === 'offline';
  if (!allowed) {
    return;
  }
  transportStatus = nextStatus;

  if (transportIndicatorBtnEl) {
    transportIndicatorBtnEl.classList.remove('status-live', 'status-fallback', 'status-reconnect', 'status-offline');
    transportIndicatorBtnEl.classList.add(`status-${nextStatus}`);
    transportIndicatorBtnEl.setAttribute('aria-label', `Transport status: ${transportStatusLabel(nextStatus)}`);
  }

  updateTransportPopover();
}

function markTransportDataUpdate() {
  transportLastUpdateMs = Date.now();
  updateTransportPopover();
}

function openTransportPopover() {
  if (!transportPopoverEl || !transportIndicatorBtnEl) {
    return;
  }
  transportPopoverEl.hidden = false;
  transportIndicatorBtnEl.setAttribute('aria-expanded', 'true');
  updateTransportPopover();
}

function closeTransportPopover() {
  if (!transportPopoverEl || !transportIndicatorBtnEl) {
    return;
  }
  transportPopoverEl.hidden = true;
  transportIndicatorBtnEl.setAttribute('aria-expanded', 'false');
}

function toggleTransportPopover() {
  if (!transportPopoverEl) {
    return;
  }
  if (transportPopoverEl.hidden) {
    openTransportPopover();
  } else {
    closeTransportPopover();
  }
}

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
  if (normalized === 'AUTO' || normalized === 'MANUAL' || normalized === 'TIME' || normalized === 'OFF') {
    return normalized;
  }
  return 'AUTO';
}

function clampNumber(value, minValue, maxValue) {
  if (!Number.isFinite(value)) {
    return minValue;
  }
  return Math.min(maxValue, Math.max(minValue, Math.round(value)));
}

function percentToPermille(percent) {
  return clampNumber(Number(percent) * 10, 0, 1000);
}

function permilleToPercent(permille) {
  return clampNumber(Number(permille) / 10, 0, 100);
}

function readConfigNumber(value, fallback, minValue, maxValue) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return clampNumber(fallback, minValue, maxValue);
  }
  return clampNumber(numeric, minValue, maxValue);
}

function formatMinutesSeconds(totalSeconds) {
  const seconds = Math.max(0, Math.floor(Number(totalSeconds) || 0));
  const minutes = Math.floor(seconds / 60);
  const secondsRemainder = seconds % 60;
  return `${minutes}m ${secondsRemainder}s`;
}

function formatClockMmSs(totalSeconds) {
  const seconds = Math.max(0, Math.floor(Number(totalSeconds) || 0));
  const minutes = Math.floor(seconds / 60);
  const secondsRemainder = seconds % 60;
  return `${String(minutes).padStart(2, '0')}:${String(secondsRemainder).padStart(2, '0')}`;
}

function formatHoursMinutes(totalMinutes) {
  const normalizedMinutes = Math.max(0, Math.floor(Number(totalMinutes) || 0));
  const hours = Math.floor(normalizedMinutes / 60);
  const minutes = normalizedMinutes % 60;
  return `${hours}h ${minutes}m`;
}

function formatHoursMinutesSeconds(totalSeconds) {
  const sec = Math.max(0, Math.floor(Number(totalSeconds) || 0));
  const hours = Math.floor(sec / 3600);
  const minutes = Math.floor((sec % 3600) / 60);
  const seconds = sec % 60;
  if (hours > 0) {
    return `${hours}h ${minutes}m ${seconds}s`;
  }
  return `${minutes}m ${seconds}s`;
}

function formatMinutesValueFromSeconds(totalSeconds) {
  const normalizedSec = Math.max(MANUAL_DURATION_MIN_SEC, Math.min(MANUAL_DURATION_MAX_SEC, Number(totalSeconds) || 0));
  const minutes = normalizedSec / 60;
  if (Number.isInteger(minutes)) {
    return String(minutes);
  }
  return minutes.toFixed(2).replace(/\.0+$/, '').replace(/(\.\d*?)0+$/, '$1');
}

function parseMinutesToSeconds(inputValue, fallbackSeconds = 0) {
  const normalizedInput = String(inputValue ?? '').trim().replace(',', '.');
  if (normalizedInput.length === 0) {
    return clampNumber(fallbackSeconds, MANUAL_DURATION_MIN_SEC, MANUAL_DURATION_MAX_SEC);
  }

  const parsedMinutes = Number.parseFloat(normalizedInput);
  if (!Number.isFinite(parsedMinutes)) {
    return clampNumber(fallbackSeconds, MANUAL_DURATION_MIN_SEC, MANUAL_DURATION_MAX_SEC);
  }

  const clampedMinutes = Math.min(MANUAL_DURATION_MAX_SEC / 60, Math.max(MANUAL_DURATION_MIN_SEC / 60, parsedMinutes));
  const roundedSeconds = Math.round(clampedMinutes * 60);
  return clampNumber(roundedSeconds, MANUAL_DURATION_MIN_SEC, MANUAL_DURATION_MAX_SEC);
}

function parseLocalizedDecimal(inputValue) {
  const normalizedInput = String(inputValue ?? '').trim().replace(',', '.');
  if (normalizedInput.length === 0) {
    return Number.NaN;
  }
  return Number.parseFloat(normalizedInput);
}

function parseHoursToMinutes(inputValue, fallbackMinutes = 0) {
  const parsedHours = parseLocalizedDecimal(inputValue);
  if (!Number.isFinite(parsedHours)) {
    return clampNumber(fallbackMinutes, TIME_INTERVAL_MIN, TIME_INTERVAL_MAX);
  }
  const clampedHours = Math.min(TIME_INTERVAL_MAX / 60, Math.max(TIME_INTERVAL_MIN / 60, parsedHours));
  return clampNumber(Math.round(clampedHours * 60), TIME_INTERVAL_MIN, TIME_INTERVAL_MAX);
}

function parseMinutesToRoundedSeconds(inputValue, fallbackSeconds = 0) {
  const parsedMinutes = parseLocalizedDecimal(inputValue);
  if (!Number.isFinite(parsedMinutes)) {
    return clampNumber(fallbackSeconds, TIME_RUN_MIN_SEC, TIME_RUN_MAX_SEC);
  }
  const clampedMinutes = Math.min(TIME_RUN_MAX_SEC / 60, Math.max(TIME_RUN_MIN_SEC / 60, parsedMinutes));
  return clampNumber(Math.round(clampedMinutes * 60), TIME_RUN_MIN_SEC, TIME_RUN_MAX_SEC);
}

function formatHoursValueFromMinutes(totalMinutes) {
  const hours = Math.max(TIME_INTERVAL_MIN, Math.min(TIME_INTERVAL_MAX, Number(totalMinutes) || 0)) / 60;
  if (Number.isInteger(hours)) {
    return String(hours);
  }
  return hours.toFixed(2).replace(/\.0+$/, '').replace(/(\.\d*?)0+$/, '$1');
}

function blendRemainingSeconds(localSec, serverSec, forceServer = false) {
  const server = Math.max(0, Math.floor(Number(serverSec) || 0));
  const local = Math.max(0, Math.floor(Number(localSec) || 0));
  if (forceServer) {
    return server;
  }
  const drift = server - local;
  if (Math.abs(drift) >= COUNTDOWN_RESYNC_THRESHOLD_SEC) {
    if (drift > 0) {
      return local + 1;
    }
    return Math.max(0, local - 1);
  }
  return local;
}

function blendIncreasingSeconds(localSec, serverSec, forceServer = false) {
  const server = Math.max(0, Math.floor(Number(serverSec) || 0));
  const local = Math.max(0, Math.floor(Number(localSec) || 0));
  if (forceServer) {
    return server;
  }
  if (server + 1 < local) {
    return local - 1;
  }
  if (server > local + COUNTDOWN_RESYNC_THRESHOLD_SEC) {
    return local + 1;
  }
  return Math.max(local, server);
}

function isHomeModeDirty() {
  const selectedMode = normalizeIrrigationMode(pendingIrrigationMode);
  return isModeSelectionDirty() || isModeSettingsDirty(selectedMode);
}

function isModeSelectionDirty() {
  return normalizeIrrigationMode(pendingIrrigationMode) !== normalizeIrrigationMode(persistedIrrigationMode);
}

function isModeSettingsDirty(mode) {
  const normalizedMode = normalizeIrrigationMode(mode);
  if (normalizedMode === 'AUTO') {
    return pendingAutoStartPermille !== persistedAutoStartPermille
      || pendingAutoStopPermille !== persistedAutoStopPermille;
  }
  if (normalizedMode === 'TIME') {
    return pendingTimeIntervalMin !== persistedTimeIntervalMin
      || pendingTimeRunDurationSec !== persistedTimeRunDurationSec;
  }
  return false;
}

function setHomeModeStatus(text, isError = false) {
  if (!homeModeStatusEl) {
    return;
  }
  homeModeStatusEl.textContent = text;
  homeModeStatusEl.classList.toggle('error', isError);
}

function clearStaleHomeModeFetchError() {
  if (!homeModeStatusEl || !homeModeStatusEl.classList.contains('error')) {
    return;
  }

  const currentText = String(homeModeStatusEl.textContent || '');
  if (!currentText.startsWith('Config fetch error:')) {
    return;
  }

  setHomeModeStatus(homeModeStatusText(), false);
}

function clearStaleManualFetchError() {
  if (!homeManualStatusEl || !homeManualStatusEl.classList.contains('error')) {
    return;
  }

  const currentText = String(homeManualStatusEl.textContent || '');
  if (!currentText.startsWith('Manual control unavailable:')) {
    return;
  }

  setHomeManualStatus(manualStatusFromControlState(), false);
}

function clearStaleControlAvailabilityFetchError() {
  if (!homeControlLockoutStatusEl || !homeControlLockoutStatusEl.classList.contains('error')) {
    return;
  }

  const currentText = String(homeControlLockoutStatusEl.textContent || '');
  if (!currentText.startsWith('Control availability unavailable:')) {
    return;
  }

  updateControlAvailabilityStatus();
}

function clearStaleFetchErrorsFromFreshSnapshot() {
  clearStaleHomeModeFetchError();
  clearStaleManualFetchError();
  clearStaleControlAvailabilityFetchError();
}

function homeModeStatusText() {
  const selectedMode = normalizeIrrigationMode(pendingIrrigationMode);
  if (isModeSettingsDirty(selectedMode)) {
    return 'Unsaved changes.';
  }
  if (isModeSelectionDirty()) {
    return '';
  }
  return 'Config synced.';
}

function setHomeManualStatus(text, isError = false, resetAfterMs = 0) {
  if (!homeManualStatusEl) {
    return;
  }
  if (homeManualStatusResetTimer && resetAfterMs === 0 && !isError) {
    return;
  }
  if (homeManualStatusResetTimer) {
    clearTimeout(homeManualStatusResetTimer);
    homeManualStatusResetTimer = 0;
  }
  homeManualStatusEl.textContent = text;
  homeManualStatusEl.classList.toggle('error', isError);
  if (resetAfterMs > 0) {
    homeManualStatusResetTimer = setTimeout(() => {
      homeManualStatusEl.textContent = manualStatusFromControlState();
      homeManualStatusEl.classList.remove('error');
      homeManualStatusResetTimer = 0;
    }, resetAfterMs);
  }
}

function manualStatusFromControlState() {
  if (controlConfirmedState === 'pending_start') {
    return `Starting... ${Math.max(0, Math.floor(controlPendingElapsedSec))}s`;
  }
  if (controlConfirmedState === 'pending_stop') {
    return `Stopping... ${Math.max(0, Math.floor(controlPendingElapsedSec))}s`;
  }
  if (controlConfirmedState === 'lost') {
    return 'Control lost. Watering forced OFF in UI.';
  }
  if (isManualIrrigationActive) {
    return `Watering in progress (${formatClockMmSs(manualRunRemainingSec)})`;
  }
  return 'Manual watering inactive.';
}

function setHomeControlLockoutStatus(text, isError = false) {
  if (!homeControlLockoutStatusEl) {
    return;
  }
  homeControlLockoutStatusEl.textContent = text;
  homeControlLockoutStatusEl.classList.toggle('error', isError);
}

function setTrackExportStatus(text, isError = false) {
  if (!trackExportStatusEl) {
    return;
  }
  trackExportStatusEl.textContent = text;
  trackExportStatusEl.classList.toggle('error', isError);
}

function buildTrackExportFileName() {
  const now = new Date();
  const pad = (value) => String(value).padStart(2, '0');
  const stamp = `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}_${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
  const uptimePart = Number.isFinite(latestSummaryUptimeSec) ? `_uptime${Math.floor(latestSummaryUptimeSec)}s` : '';
  return `track_export_${stamp}${uptimePart}.csv`;
}

async function exportTrackCsv() {
  if (trackExportCsvBtnEl) {
    trackExportCsvBtnEl.disabled = true;
  }
  setTrackExportStatus('Preparing CSV export...', false);

  try {
    const response = await fetch('/api/track/export.csv', {
      method: 'GET',
      cache: 'no-store',
    });

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const blob = await response.blob();
    const fileName = buildTrackExportFileName();
    const blobUrl = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = blobUrl;
    anchor.download = fileName;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    URL.revokeObjectURL(blobUrl);

    setTrackExportStatus(`CSV exported: ${fileName}`, false);
  } catch (error) {
    setTrackExportStatus(`CSV export failed: ${error.message}`, true);
  } finally {
    if (trackExportCsvBtnEl) {
      trackExportCsvBtnEl.disabled = false;
    }
  }
}

function updateControlAvailabilityStatus() {
  isControlLowBatteryLockoutActive = controlAvailabilityStatus === 'battery_lockout';

  if (controlAvailabilityStatus === 'online') {
    setHomeControlLockoutStatus('Control status: online. Wake ETA unavailable (sleep policy not defined).', false);
    return;
  }
  if (controlAvailabilityStatus === 'battery_lockout') {
    setHomeControlLockoutStatus('Control status: battery lockout. Watering blocked. Wake ETA unavailable (sleep policy not defined).', true);
    return;
  }
  if (controlAvailabilityStatus === 'offline') {
    setHomeControlLockoutStatus('Control status: offline. Watering blocked. Wake ETA unavailable (sleep policy not defined).', true);
    return;
  }
  setHomeControlLockoutStatus('Control status: not paired. Watering blocked. Wake ETA unavailable (sleep policy not defined).', true);
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

  pendingManualDurationSec = clampNumber(pendingManualDurationSec, MANUAL_DURATION_MIN_SEC, MANUAL_DURATION_MAX_SEC);
  pendingAutoStartPermille = clampNumber(pendingAutoStartPermille, 0, 1000);
  pendingAutoStopPermille = clampNumber(pendingAutoStopPermille, 0, 1000);
  if (pendingAutoStopPermille <= pendingAutoStartPermille) {
    pendingAutoStopPermille = Math.min(1000, pendingAutoStartPermille + 50);
  }
  pendingTimeIntervalMin = clampNumber(pendingTimeIntervalMin, TIME_INTERVAL_MIN, TIME_INTERVAL_MAX);
  pendingTimeRunDurationSec = clampNumber(pendingTimeRunDurationSec, TIME_RUN_MIN_SEC, TIME_RUN_MAX_SEC);

  const selectedMode = normalizeIrrigationMode(pendingIrrigationMode);
  const isModeDirty = isModeSelectionDirty();
  const isSettingsDirty = isModeSettingsDirty(selectedMode);
  const radios = homeModeFormEl.querySelectorAll('input[name="irrigationMode"]');
  radios.forEach((radio) => {
    radio.checked = radio.value === selectedMode;
    const wrapper = radio.closest('label');
    if (wrapper) {
      wrapper.classList.toggle('persisted', radio.value === normalizeIrrigationMode(persistedIrrigationMode));
    }
  });

  const isDirty = isModeDirty || isSettingsDirty;
  const isManualMode = selectedMode === 'MANUAL';
  const inactiveLabel = selectedMode === 'OFF' ? 'Disabled' : 'Active';
  const actionLabel = selectedMode === 'OFF' ? 'Disable' : 'Activate';

  if (isManualMode) {
    homeModeSaveBtnEl.hidden = true;
    homeModeSaveBtnEl.disabled = true;
  } else {
    homeModeSaveBtnEl.hidden = false;
    homeModeSaveBtnEl.disabled = !isDirty;
    if (isModeDirty) {
      homeModeSaveBtnEl.textContent = actionLabel;
    } else if (isSettingsDirty) {
      homeModeSaveBtnEl.textContent = 'Save settings';
    } else {
      homeModeSaveBtnEl.textContent = inactiveLabel;
    }
  }
  homeModeSaveBtnEl.classList.toggle('saved', isManualMode || !isDirty);

  if (homeModeStatusEl) {
    homeModeStatusEl.hidden = isManualMode;
  }

  const showManualActions = selectedMode === 'MANUAL';
  const showAutoConfig = selectedMode === 'AUTO';
  const showManualConfig = selectedMode === 'MANUAL';
  const showTimeConfig = selectedMode === 'TIME';

  if (homeAutoConfigEl) {
    homeAutoConfigEl.hidden = !showAutoConfig;
  }

  if (homeManualConfigEl) {
    homeManualConfigEl.hidden = !showManualConfig;
  }

  if (homeTimeConfigEl) {
    homeTimeConfigEl.hidden = !showTimeConfig;
  }

  if (homeManualActionsEl) {
    homeManualActionsEl.hidden = !showManualActions;
  }

  if (manualStartBtnEl && manualStopBtnEl) {
    const secondsLeft = Math.max(0, Math.floor(manualRunRemainingSec));
    const isStartBlocked = manualStartBlockedReason !== 'none';
    const isPendingStart = controlConfirmedState === 'pending_start';
    const isPendingStop = controlConfirmedState === 'pending_stop';

    manualStartBtnEl.disabled = !showManualActions || isManualIrrigationActive || isStartBlocked || isPendingStart || isPendingStop;
    manualStartBtnEl.textContent = isPendingStart
      ? `Starting... ${Math.max(0, Math.floor(controlPendingElapsedSec))}s`
      : (isManualIrrigationActive
        ? `Watering ${formatClockMmSs(secondsLeft)}`
        : 'Start watering');
    manualStartBtnEl.classList.toggle('watering-active', isManualIrrigationActive);
    manualStartBtnEl.classList.toggle('is-pending', isPendingStart);

    manualStopBtnEl.disabled = !showManualActions || (!isManualIrrigationActive && !isPendingStart) || isPendingStop;
    manualStopBtnEl.textContent = isPendingStop
      ? `Stopping... ${Math.max(0, Math.floor(controlPendingElapsedSec))}s`
      : 'Stop watering';
    manualStopBtnEl.classList.toggle('is-pending', isPendingStop);
  }

  if (showManualActions && homeManualStatusEl) {
    if (homeManualStatusResetTimer) {
      clearTimeout(homeManualStatusResetTimer);
      homeManualStatusResetTimer = 0;
    }
    homeManualStatusEl.textContent = manualStatusFromControlState();
    homeManualStatusEl.classList.toggle('error', controlConfirmedState === 'lost');
  }

  if (manualDurationSecInputEl && document.activeElement !== manualDurationSecInputEl) {
    manualDurationSecInputEl.value = formatMinutesValueFromSeconds(pendingManualDurationSec);
  }
  if (manualDurationHintEl) {
    manualDurationHintEl.hidden = false;
    manualDurationHintEl.textContent = `Duration: ${formatMinutesSeconds(pendingManualDurationSec)}`;
  }
  if (autoStartMoisturePctInputEl && document.activeElement !== autoStartMoisturePctInputEl) {
    autoStartMoisturePctInputEl.value = String(permilleToPercent(pendingAutoStartPermille));
  }
  if (autoStopMoisturePctInputEl && document.activeElement !== autoStopMoisturePctInputEl) {
    autoStopMoisturePctInputEl.value = String(permilleToPercent(pendingAutoStopPermille));
  }
  if (autoStatusHintEl) {
    autoStatusHintEl.hidden = !showAutoConfig;
    if (showAutoConfig) {
      const isPersistedAutoMode = normalizeIrrigationMode(persistedIrrigationMode) === 'AUTO';
      const startPct = permilleToPercent(pendingAutoStartPermille);
      const stopPct = permilleToPercent(pendingAutoStopPermille);
      const moisturePct = latestAvgMoisturePermille !== null ? Math.round(latestAvgMoisturePermille / 10) : null;
      if (!isModeDirty && !isSettingsDirty && isPersistedAutoMode) {
        if (isManualIrrigationActive) {
          const stopStr = moisturePct !== null ? `stops above ${stopPct}%.` : `stops above ${stopPct}%.`;
          autoStatusHintEl.textContent = moisturePct !== null
            ? `Watering now, ${stopStr} Moisture: ${moisturePct}%.`
            : `Watering now, ${stopStr}`;
        } else if (moisturePct !== null) {
          if (moisturePct <= startPct) {
            autoStatusHintEl.textContent = `Moisture: ${moisturePct}% — starting irrigation.`;
          } else {
            autoStatusHintEl.textContent = `Moisture: ${moisturePct}% — watering starts below ${startPct}%.`;
          }
        } else {
          autoStatusHintEl.textContent = `Watering starts below ${startPct}%, stops above ${stopPct}%.`;
        }
      } else {
        autoStatusHintEl.textContent = `Start below ${startPct}%, stop above ${stopPct}%.`;
      }
    }
  }
  if (timeIntervalMinInputEl && document.activeElement !== timeIntervalMinInputEl) {
    timeIntervalMinInputEl.value = formatHoursValueFromMinutes(pendingTimeIntervalMin);
    timeIntervalMinInputEl.classList.remove('input-warning');
  }
  if (timeRunDurationMinInputEl && document.activeElement !== timeRunDurationMinInputEl) {
    timeRunDurationMinInputEl.value = formatMinutesValueFromSeconds(pendingTimeRunDurationSec);
  }
  if (timeRunDurationHintEl) {
    timeRunDurationHintEl.hidden = !showTimeConfig;
    if (showTimeConfig) {
      timeRunDurationHintEl.textContent = `Duration: ${formatMinutesSeconds(pendingTimeRunDurationSec)}`;
    }
  }
  if (timeIntervalHintEl) {
    timeIntervalHintEl.hidden = !showTimeConfig;
    if (showTimeConfig) {
      const isIntervalWarning = timeIntervalMinInputEl && timeIntervalMinInputEl.classList.contains('input-warning');
      if (isIntervalWarning) {
        timeIntervalHintEl.textContent = `Minimum interval: 5 min (0.083 h)`;
        timeIntervalHintEl.classList.add('error');
      } else {
        timeIntervalHintEl.textContent = `Interval: ${formatHoursMinutes(pendingTimeIntervalMin)}`;
        timeIntervalHintEl.classList.remove('error');
      }
    }
  }
  if (timeScheduleHintEl) {
    const isPersistedTimeMode = normalizeIrrigationMode(persistedIrrigationMode) === 'TIME';
    timeScheduleHintEl.hidden = !showTimeConfig;
    if (showTimeConfig) {
      let statusText = '';
      if (!isModeDirty && !isSettingsDirty && isPersistedTimeMode) {
        if (timeRunRemainingSec > 0) {
          statusText = `Watering now, ${formatHoursMinutesSeconds(timeRunRemainingSec)} left. Next cycle in ${formatHoursMinutesSeconds(timeNextStartRemainingSec)}.`;
        } else if (timeNextStartRemainingSec > 0) {
          statusText = `Next watering in ${formatHoursMinutesSeconds(timeNextStartRemainingSec)}.`;
        } else {
          statusText = 'Schedule active.';
        }
      } else if (!isModeDirty && !isSettingsDirty) {
        statusText = 'Schedule not active.';
      }
      timeScheduleHintEl.textContent = statusText;
    }
  }
}

function applyIrrigationConfig(config, allowOverridePending = true) {
  const nowMs = Date.now();
  const prevManualActive = isManualIrrigationActive;
  const prevControlState = controlConfirmedState;
  const mode = normalizeIrrigationMode(config && config.mode);
  persistedIrrigationMode = mode;
  persistedManualDurationSec = readConfigNumber(config && config.manualDurationSec, persistedManualDurationSec, MANUAL_DURATION_MIN_SEC, MANUAL_DURATION_MAX_SEC);
  persistedAutoStartPermille = readConfigNumber(config && config.autoStartPermille, persistedAutoStartPermille, 0, 1000);
  persistedAutoStopPermille = readConfigNumber(config && config.autoStopPermille, persistedAutoStopPermille, 0, 1000);
  if (persistedAutoStopPermille <= persistedAutoStartPermille) {
    persistedAutoStopPermille = Math.min(1000, persistedAutoStartPermille + 50);
  }
  persistedTimeIntervalMin = readConfigNumber(config && config.timeIntervalMin, persistedTimeIntervalMin, TIME_INTERVAL_MIN, TIME_INTERVAL_MAX);
  persistedTimeRunDurationSec = readConfigNumber(
    config && (typeof config.timeRunDurationSec !== 'undefined' ? config.timeRunDurationSec : Number(config.timeRunDurationMin) * 60),
    persistedTimeRunDurationSec,
    TIME_RUN_MIN_SEC,
    TIME_RUN_MAX_SEC,
  );
  const serverTimeNextStartSec = readConfigNumber(config && config.timeNextStartSec, 0, 0, TIME_INTERVAL_MAX * 60);
  const serverTimeRunRemainingSec = readConfigNumber(config && config.runRemainingSec, 0, 0, TIME_RUN_MAX_SEC);
  const serverManualActive = Boolean(config && config.manualActive);
  const serverConfirmedState = String(config && config.confirmedState ? config.confirmedState : 'idle').toLowerCase();
  const serverPendingElapsedSec = readConfigNumber(config && config.pendingElapsedSec, controlPendingElapsedSec, 0, 3600);
  const serverManualRunRemainingSec = readConfigNumber(config && config.runRemainingSec, manualRunRemainingSec, 0, MANUAL_DURATION_MAX_SEC);

  let resolvedConfirmedState = serverConfirmedState;
  let resolvedPendingElapsedSec = serverPendingElapsedSec;
  const localPendingElapsedSec = localManualPendingSinceMs > 0
    ? Math.max(0, Math.floor((nowMs - localManualPendingSinceMs) / 1000))
    : 0;
  const withinPendingGuard = localPendingElapsedSec <= MANUAL_PENDING_GUARD_SEC;

  if (localManualPendingCommand === 'start') {
    if (serverConfirmedState === 'idle' && !serverManualActive && withinPendingGuard) {
      resolvedConfirmedState = 'pending_start';
      resolvedPendingElapsedSec = Math.max(serverPendingElapsedSec, localPendingElapsedSec);
    }
    if (serverManualActive || serverConfirmedState === 'active' || serverConfirmedState === 'pending_stop' || serverConfirmedState === 'lost' || !withinPendingGuard) {
      localManualPendingCommand = 'none';
      localManualPendingSinceMs = 0;
    }
  } else if (localManualPendingCommand === 'stop') {
    if (serverManualActive && serverConfirmedState !== 'pending_stop' && withinPendingGuard) {
      resolvedConfirmedState = 'pending_stop';
      resolvedPendingElapsedSec = Math.max(serverPendingElapsedSec, localPendingElapsedSec);
    }
    if (!serverManualActive || serverConfirmedState === 'lost' || !withinPendingGuard) {
      localManualPendingCommand = 'none';
      localManualPendingSinceMs = 0;
    }
  }

  const effectiveManualActive = serverManualActive || resolvedConfirmedState === 'active';
  if (config && typeof config.manualActive !== 'undefined') {
    isManualIrrigationActive = effectiveManualActive;
  }
  controlAvailabilityStatus = String(config && config.control && config.control.status ? config.control.status : 'not_paired').toLowerCase();
  manualStartBlockedReason = String(config && config.manualBlockedReason ? config.manualBlockedReason : 'control_not_paired').toLowerCase();
  const controlStateChanged = resolvedConfirmedState !== prevControlState;
  const manualStateChanged = isManualIrrigationActive !== prevManualActive;

  manualRunRemainingSec = blendRemainingSeconds(
    manualRunRemainingSec,
    serverManualRunRemainingSec,
    manualStateChanged || controlStateChanged,
  );
  controlConfirmedState = resolvedConfirmedState;
  controlPendingElapsedSec = blendIncreasingSeconds(
    controlPendingElapsedSec,
    resolvedPendingElapsedSec,
    controlStateChanged,
  );
  // TIME schedule countdowns should follow a single authoritative source
  // from backend snapshots to avoid visual oscillation.
  timeRunRemainingSec = Math.max(0, Math.floor(Number(serverTimeRunRemainingSec) || 0));
  timeNextStartRemainingSec = Math.max(0, Math.floor(Number(serverTimeNextStartSec) || 0));
  if (!isManualIrrigationActive) {
    manualRunRemainingSec = 0;
  }
  if (allowOverridePending) {
    pendingIrrigationMode = mode;
    if (!manualDurationDirty) {
      pendingManualDurationSec = persistedManualDurationSec;
    }
    pendingAutoStartPermille = persistedAutoStartPermille;
    pendingAutoStopPermille = persistedAutoStopPermille;
    pendingTimeIntervalMin = persistedTimeIntervalMin;
    pendingTimeRunDurationSec = persistedTimeRunDurationSec;
  }

  updateControlAvailabilityStatus();
  renderHomeModeControls();
  setHomeManualStatus(manualStatusFromControlState(), controlConfirmedState === 'lost');
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

  if (mode === 'MANUAL') {
    return;
  }

  if (pendingAutoStopPermille <= pendingAutoStartPermille) {
    setHomeModeStatus('Auto stop moisture must be above auto start moisture.', true);
    return;
  }

  try {
    await postForm('/api/irrigation/config', {
      mode,
      manualDurationSec: pendingManualDurationSec,
      autoStartPermille: pendingAutoStartPermille,
      autoStopPermille: pendingAutoStopPermille,
      timeIntervalMin: pendingTimeIntervalMin,
      timeRunDurationSec: pendingTimeRunDurationSec,
    });
    persistedIrrigationMode = mode;
    persistedManualDurationSec = pendingManualDurationSec;
    persistedAutoStartPermille = pendingAutoStartPermille;
    persistedAutoStopPermille = pendingAutoStopPermille;
    persistedTimeIntervalMin = pendingTimeIntervalMin;
    persistedTimeRunDurationSec = pendingTimeRunDurationSec;
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
  latestAvgMoisturePermille = Number.isFinite(Number(avgMoisturePermille)) ? Number(avgMoisturePermille) : null;
  const pairingOpen = Boolean(summary.pairingOpen);
  const pairingSec = Number(summary.pairingRemainingSec);
  const uptimeSec = Number(summary.uptimeSec);
  latestSummaryUptimeSec = Number.isFinite(uptimeSec) ? uptimeSec : null;

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
  if (manualStartBlockedReason !== 'none') {
    if (manualStartBlockedReason === 'control_battery_lockout') {
      setHomeManualStatus('Watering blocked: control battery lockout is active.', true, 5000);
    } else if (manualStartBlockedReason === 'control_offline') {
      setHomeManualStatus('Watering blocked: control is offline.', true, 5000);
    } else {
      setHomeManualStatus('Watering blocked: control is not paired.', true, 5000);
    }
    return;
  }

  try {
    await postForm('/api/irrigation/manual/start', { durationSec: pendingManualDurationSec });
    persistedManualDurationSec = pendingManualDurationSec;
    manualDurationDirty = false;
    localManualPendingCommand = 'start';
    localManualPendingSinceMs = Date.now();
    controlConfirmedState = 'pending_start';
    controlPendingElapsedSec = 0;
    renderHomeModeControls();
    await tick();
    setHomeManualStatus(manualStatusFromControlState(), false, 5000);
  } catch (error) {
    setHomeManualStatus(`Watering start failed: ${error.message}`, true);
  }
}

async function stopManualIrrigation() {
  try {
    await postForm('/api/irrigation/manual/stop', {});
    localManualPendingCommand = 'stop';
    localManualPendingSinceMs = Date.now();
    controlConfirmedState = 'pending_stop';
    controlPendingElapsedSec = 0;
    renderHomeModeControls();
    await tick();
    setHomeManualStatus(manualStatusFromControlState(), false, 5000);
  } catch (error) {
    setHomeManualStatus(`Watering stop failed: ${error.message}`, true);
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

function applyDashboardSnapshot(snapshot, allowOverridePending = true) {
  const webStatus = snapshot && snapshot.webStatus ? snapshot.webStatus : null;
  const nodes = Array.isArray(snapshot && snapshot.nodes) ? snapshot.nodes : [];
  const summary = snapshot && snapshot.summary ? snapshot.summary : null;
  const irrigationConfig = snapshot && snapshot.irrigationConfig ? snapshot.irrigationConfig : null;
  const unitStatus = snapshot && snapshot.unitStatus ? snapshot.unitStatus : null;
  markTransportDataUpdate();

  // Fresh snapshot (SSE or polling) should clear transient fetch errors
  // that might have been shown while the head was rebooting.
  clearStaleFetchErrorsFromFreshSnapshot();

  latestNodes = nodes;

  if (webStatus) {
    render(webStatusEl, webStatus);
    syncPairingCountdown(webStatus);
  }

  renderHomeSummary(summary);
  if (irrigationConfig) {
    applyIrrigationConfig(irrigationConfig, allowOverridePending);
  }

  if (unitStatus) {
    applyUnitStatus(unitStatus, !unitNameSaveBtnEl || unitNameSaveBtnEl.disabled);
  }

  renderNodes(nodes);
  reconcileCalibrationState();
  renderCalibrationBanner();
  placeCalibrationBanner();
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
    applyDashboardSnapshot({
      webStatus,
      nodes,
      summary,
      irrigationConfig,
      unitStatus,
    }, !isHomeModeDirty());
    if (!irrigationEventsSource) {
      setTransportStatus('fallback');
    }
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
    setHomeControlLockoutStatus(`Control availability unavailable: ${error}`, true);
    activeCalibration = null;
    calibrationStateByNode.clear();
    renderCalibrationBanner();
    if (!irrigationEventsSource && tickIntervalId) {
      setTransportStatus('offline');
    }
  }
}

function stopTickPolling() {
  if (!tickIntervalId) {
    return;
  }
  clearInterval(tickIntervalId);
  tickIntervalId = 0;
  activeTickIntervalMs = 0;
  updateTransportPopover();
}

function ensureTickPolling(intervalMs) {
  const normalized = Math.max(500, Math.floor(Number(intervalMs) || 0));
  if (tickIntervalId && activeTickIntervalMs === normalized) {
    return;
  }
  stopTickPolling();
  tickIntervalId = setInterval(tick, normalized);
  activeTickIntervalMs = normalized;
  if (!irrigationEventsSource) {
    setTransportStatus('fallback');
  } else {
    updateTransportPopover();
  }
}

function connectIrrigationEvents() {
  if (typeof window === 'undefined' || typeof window.EventSource === 'undefined') {
    return;
  }

  if (irrigationEventsSource) {
    irrigationEventsSource.close();
    irrigationEventsSource = null;
  }

  const source = new EventSource('/api/events');
  irrigationEventsSource = source;
  setTransportStatus('reconnect');

  source.onopen = () => {
    stopTickPolling();
    setTransportStatus('live');
  };

  source.addEventListener('snapshot', (event) => {
    try {
      const snapshot = JSON.parse(event.data);
      applyDashboardSnapshot(snapshot, !isHomeModeDirty());
      setTransportStatus('live');
    } catch (error) {
      if (webStatusEl) {
        webStatusEl.textContent = `events parse error: ${error}`;
      }
    }
  });

  source.onerror = () => {
    if (irrigationEventsSource === source) {
      irrigationEventsSource = null;
    }
    source.close();
    setTransportStatus('reconnect');
    ensureTickPolling(POLL_INTERVAL_FAST_MS);
    if (irrigationEventsReconnectTimer) {
      return;
    }
    transportReconnectAttempts += 1;
    updateTransportPopover();
    irrigationEventsReconnectTimer = setTimeout(() => {
      irrigationEventsReconnectTimer = 0;
      connectIrrigationEvents();
    }, 2000);
  };
}

if (transportIndicatorBtnEl) {
  transportIndicatorBtnEl.addEventListener('click', (event) => {
    event.stopPropagation();
    toggleTransportPopover();
  });
}

if (transportPopoverEl) {
  transportPopoverEl.addEventListener('click', (event) => {
    event.stopPropagation();
  });
}

document.addEventListener('click', (event) => {
  if (transportPopoverEl && !transportPopoverEl.hidden) {
    const target = event.target;
    if (target instanceof Node
      && transportPopoverEl
      && !transportPopoverEl.contains(target)
      && transportIndicatorBtnEl
      && !transportIndicatorBtnEl.contains(target)) {
      closeTransportPopover();
    }
  }
});

document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') {
    closeTransportPopover();
  }
});

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
    setHomeModeStatus(homeModeStatusText(), false);
  });
}

if (manualDurationSecInputEl) {
  manualDurationSecInputEl.addEventListener('input', () => {
    pendingManualDurationSec = parseMinutesToSeconds(manualDurationSecInputEl.value, pendingManualDurationSec);
    manualDurationDirty = true;
    renderHomeModeControls();
    setHomeManualStatus(`Next manual watering duration: ${formatMinutesSeconds(pendingManualDurationSec)}.`, false, 2500);
  });
}

if (autoStartMoisturePctInputEl) {
  autoStartMoisturePctInputEl.addEventListener('input', () => {
    pendingAutoStartPermille = percentToPermille(autoStartMoisturePctInputEl.value);
    renderHomeModeControls();
    setHomeModeStatus(homeModeStatusText(), false);
  });
}

if (autoStopMoisturePctInputEl) {
  autoStopMoisturePctInputEl.addEventListener('input', () => {
    pendingAutoStopPermille = percentToPermille(autoStopMoisturePctInputEl.value);
    renderHomeModeControls();
    setHomeModeStatus(homeModeStatusText(), false);
  });
}

if (timeIntervalMinInputEl) {
  timeIntervalMinInputEl.addEventListener('input', () => {
    const rawHours = parseLocalizedDecimal(timeIntervalMinInputEl.value);
    const roundedMin = Number.isFinite(rawHours) ? Math.round(rawHours * 60) : TIME_INTERVAL_MIN;
    const belowMin = roundedMin < TIME_INTERVAL_MIN;
    timeIntervalMinInputEl.classList.toggle('input-warning', belowMin);
    pendingTimeIntervalMin = parseHoursToMinutes(timeIntervalMinInputEl.value, pendingTimeIntervalMin);
    renderHomeModeControls();
    setHomeModeStatus(homeModeStatusText(), false);
  });
}

if (timeRunDurationMinInputEl) {
  timeRunDurationMinInputEl.addEventListener('input', () => {
    pendingTimeRunDurationSec = parseMinutesToRoundedSeconds(timeRunDurationMinInputEl.value, pendingTimeRunDurationSec);
    renderHomeModeControls();
    setHomeModeStatus(homeModeStatusText(), false);
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

ensureTickPolling(POLL_INTERVAL_FAST_MS);
tick();
connectIrrigationEvents();
setTransportStatus('reconnect');

setInterval(() => {
  updateTransportPopover();
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

if (trackExportCsvBtnEl) {
  trackExportCsvBtnEl.addEventListener('click', async () => {
    await exportTrackCsv();
  });
}
