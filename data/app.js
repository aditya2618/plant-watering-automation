const byId = (id) => document.getElementById(id);
let latestStatus = null;
let latestSettings = null;
const GAUGE_CIRCUMFERENCE = 427.26; // 2 * PI * 68

async function api(path, method = 'GET', data) {
  const options = {
    method,
    headers: { 'Content-Type': 'application/json' },
    body: data ? JSON.stringify(data) : undefined,
  };
  const response = await fetch(path, options);
  const result = await response.json();
  if (!response.ok) {
    throw new Error(result.error || 'Server request failed');
  }
  return result;
}

function showMessage(message, type = 'info') {
  const target = byId('message');
  if (!target) return;
  target.textContent = message;
  target.className = 'message-text';
  if (type === 'error') target.classList.add('error');
  if (type === 'success') target.classList.add('success');
}

function formatUptime(seconds) {
  if (!seconds || seconds < 0) return 'Just started';
  const hrs = Math.floor(seconds / 3600);
  const mins = Math.floor((seconds % 3600) / 60);
  const secs = Math.floor(seconds % 60);
  if (hrs > 0) return `${hrs}h ${mins}m`;
  if (mins > 0) return `${mins}m ${secs}s`;
  return `${secs}s`;
}

function updateMoistureGauge(moisture, isValid, fault) {
  const gaugeFill = byId('gauge-fill');
  const moistureText = byId('moisture');
  const badge = byId('moisture-badge');
  const label = byId('sensor-state');
  const meterBar = byId('meter-fill');

  if (!isValid) {
    if (gaugeFill) gaugeFill.style.strokeDashoffset = GAUGE_CIRCUMFERENCE;
    if (moistureText) moistureText.textContent = '--%';
    if (label) label.textContent = fault || 'Sensor uncalibrated';
    if (meterBar) meterBar.style.width = '0%';
    if (badge) {
      badge.textContent = fault || 'Uncalibrated';
      badge.className = 'status-pill status-caution';
    }
    return;
  }

  const clamped = Math.max(0, Math.min(100, moisture));
  const offset = GAUGE_CIRCUMFERENCE - (clamped / 100) * GAUGE_CIRCUMFERENCE;
  if (gaugeFill) gaugeFill.style.strokeDashoffset = offset;
  if (moistureText) moistureText.textContent = `${clamped}%`;
  if (meterBar) meterBar.style.width = `${clamped}%`;

  if (clamped < 25) {
    if (badge) {
      badge.textContent = 'Soil Dry';
      badge.className = 'status-pill status-caution';
    }
    if (label) label.textContent = 'Needs water';
    if (gaugeFill) gaugeFill.style.stroke = '#B85D3B'; // Earth clay
  } else if (clamped <= 70) {
    if (badge) {
      badge.textContent = 'Optimal';
      badge.className = 'status-pill status-optimal';
    }
    if (label) label.textContent = 'Healthy moisture';
    if (gaugeFill) gaugeFill.style.stroke = '#2D6A4F'; // Forest green
  } else {
    if (badge) {
      badge.textContent = 'Moist Soil';
      badge.className = 'status-pill status-optimal';
    }
    if (label) label.textContent = 'Well saturated';
    if (gaugeFill) gaugeFill.style.stroke = '#40916C'; // Leaf green
  }
}

function displayStatus(status) {
  latestStatus = status;

  // Header Connection Badge
  const conn = byId('connection');
  if (conn) {
    conn.textContent = 'Online';
    conn.className = 'badge online';
  }

  // Diagnostics
  if (byId('wifi-signal') && status.rssi) {
    byId('wifi-signal').textContent = `${status.rssi} dBm`;
  }
  if (byId('device-ip') && status.ip) {
    byId('device-ip').textContent = status.ip;
  }
  if (byId('system-uptime') && status.uptimeSec !== undefined) {
    byId('system-uptime').textContent = formatUptime(status.uptimeSec);
  }

  // Moisture Gauge
  updateMoistureGauge(status.moisture, status.valid, status.fault);

  // ADC readouts
  const adcStr = status.raw !== undefined ? `ADC: ${status.raw}` : 'ADC: ----';
  if (byId('sensor-adc')) byId('sensor-adc').textContent = adcStr;
  if (byId('live-adc-display')) byId('live-adc-display').textContent = adcStr;

  // Pump & Irrigation Status
  const pumpText = byId('pump');
  const pumpState = byId('pump-state');
  const pumpIcon = byId('pump-indicator');
  const pumpModeBadge = byId('pump-mode-badge');

  if (status.pump) {
    if (pumpText) pumpText.textContent = 'WATERING';
    const remaining = Math.ceil(status.remainingMs / 1000);
    if (pumpState) pumpState.textContent = `Cycle active • ${remaining}s remaining`;
    if (pumpIcon) {
      pumpIcon.classList.remove('idle');
      pumpIcon.classList.add('active');
    }
    if (pumpModeBadge) {
      pumpModeBadge.textContent = 'Watering';
      pumpModeBadge.className = 'status-pill status-optimal';
    }
  } else {
    if (pumpText) pumpText.textContent = 'OFF';
    const stopReasonClean = (status.lastStop || 'none').replace('_', ' ');
    if (pumpState) pumpState.textContent = `Pump idle (Last stop: ${stopReasonClean})`;
    if (pumpIcon) {
      pumpIcon.classList.remove('active');
      pumpIcon.classList.add('idle');
    }
    if (pumpModeBadge) {
      pumpModeBadge.textContent = status.auto ? 'Auto Active' : 'Auto Off';
      pumpModeBadge.className = status.auto ? 'status-pill status-optimal' : 'status-pill status-neutral';
    }
  }

  // Reservoir Status
  const resBadge = byId('reservoir-state');
  if (resBadge) {
    if (status.reservoirOk === false) {
      resBadge.textContent = 'Refill Needed';
      resBadge.className = 'safety-badge low';
    } else {
      resBadge.textContent = 'Water Level OK';
      resBadge.className = 'safety-badge safe';
    }
  }

  // Auto Mode toggle sync
  const autoCheckbox = byId('auto-mode');
  if (autoCheckbox) autoCheckbox.checked = Boolean(status.auto);

  // BUTTON STATES:
  // "Water Now" is enabled whenever the pump is idle!
  const waterBtn = byId('water-now');
  if (waterBtn) {
    waterBtn.disabled = Boolean(status.pump);
  }

  // "Emergency Stop" is enabled while pump is running
  const stopBtn = byId('stop-pump');
  if (stopBtn) {
    stopBtn.disabled = !status.pump;
  }
}

async function refreshStatus() {
  try {
    const status = await api('/api/status');
    displayStatus(status);
  } catch (error) {
    const conn = byId('connection');
    if (conn) {
      conn.textContent = 'Offline';
      conn.className = 'badge offline';
    }
  }
}

async function loadSettings() {
  try {
    const settings = await api('/api/settings');
    latestSettings = settings;

    const thresholdInput = byId('threshold');
    const thresholdOutput = byId('threshold-value');
    if (thresholdInput) thresholdInput.value = settings.threshold;
    if (thresholdOutput) thresholdOutput.textContent = `${settings.threshold}%`;

    const runtimeInput = byId('runtime');
    if (runtimeInput) runtimeInput.value = settings.pumpRuntimeMs / 1000;

    const cooldownInput = byId('cooldown');
    if (cooldownInput) cooldownInput.value = settings.cooldownMs / 60000;

    const dailyLimitInput = byId('daily-limit');
    if (dailyLimitInput) dailyLimitInput.value = settings.maxDailyPumpMs / 1000;

    // Daily allowance bar
    const usedSec = (settings.dailyUsedMs / 1000).toFixed(1);
    const limitSec = (settings.maxDailyPumpMs / 1000).toFixed(0);
    const usePercent = Math.min(100, Math.round((settings.dailyUsedMs / settings.maxDailyPumpMs) * 100));

    if (byId('daily-use-text')) byId('daily-use-text').textContent = `${usedSec}s / ${limitSec}s`;
    if (byId('daily-limit-fill')) byId('daily-limit-fill').style.width = `${usePercent}%`;

    // Calibration fields
    if (byId('dry-raw')) byId('dry-raw').value = settings.dryRaw;
    if (byId('wet-raw')) byId('wet-raw').value = settings.wetRaw;
  } catch (error) {
    showMessage('Could not load settings: ' + error.message, 'error');
  }
}

function setupEventListeners() {
  // Threshold slider live output
  const threshold = byId('threshold');
  if (threshold) {
    threshold.addEventListener('input', (e) => {
      const output = byId('threshold-value');
      if (output) output.textContent = `${e.target.value}%`;
    });
  }

  // Quick Calibration Live Buttons
  const dryQuickBtn = byId('use-live-dry');
  if (dryQuickBtn) {
    dryQuickBtn.addEventListener('click', () => {
      if (latestStatus && latestStatus.raw !== undefined) {
        byId('dry-raw').value = latestStatus.raw;
        showMessage(`Set Dry ADC to current reading (${latestStatus.raw})`, 'info');
      }
    });
  }

  const wetQuickBtn = byId('use-live-wet');
  if (wetQuickBtn) {
    wetQuickBtn.addEventListener('click', () => {
      if (latestStatus && latestStatus.raw !== undefined) {
        byId('wet-raw').value = latestStatus.raw;
        showMessage(`Set Wet ADC to current reading (${latestStatus.raw})`, 'info');
      }
    });
  }

  // Water Now
  const waterBtn = byId('water-now');
  if (waterBtn) {
    waterBtn.addEventListener('click', async () => {
      if (confirm('Start one safety-limited watering cycle?')) {
        try {
          await api('/api/water', 'POST', {});
          showMessage('Watering cycle started.', 'success');
          refreshStatus();
        } catch (error) {
          showMessage(error.message, 'error');
        }
      }
    });
  }

  // Emergency Stop
  const stopBtn = byId('stop-pump');
  if (stopBtn) {
    stopBtn.addEventListener('click', async () => {
      try {
        await api('/api/stop', 'POST', {});
        showMessage('Pump stopped.', 'info');
        refreshStatus();
      } catch (error) {
        showMessage(error.message, 'error');
      }
    });
  }

  // Auto Mode Toggle
  const autoCheckbox = byId('auto-mode');
  if (autoCheckbox) {
    autoCheckbox.addEventListener('change', async (e) => {
      const isEnabled = e.target.checked;
      try {
        await api('/api/auto', 'POST', { enabled: isEnabled });
        showMessage(isEnabled ? 'Automatic watering enabled.' : 'Automatic watering disabled.', 'info');
        refreshStatus();
      } catch (error) {
        e.target.checked = !isEnabled;
        showMessage(error.message, 'error');
      }
    });
  }

  // Save Settings
  const saveSettingsBtn = byId('save-settings');
  if (saveSettingsBtn) {
    saveSettingsBtn.addEventListener('click', async () => {
      const payload = {
        threshold: +byId('threshold').value,
        pumpRuntimeMs: Math.round(+byId('runtime').value * 1000),
        cooldownMs: Math.round(+byId('cooldown').value * 60000),
        soakMs: 30000,
        maxDailyPumpMs: Math.round(+byId('daily-limit').value * 1000),
      };
      try {
        await api('/api/settings', 'POST', payload);
        showMessage('Watering settings saved successfully.', 'success');
        loadSettings();
      } catch (error) {
        showMessage(error.message, 'error');
      }
    });
  }

  // Save Calibration
  const saveCalBtn = byId('save-calibration');
  if (saveCalBtn) {
    saveCalBtn.addEventListener('click', async () => {
      const dryVal = +byId('dry-raw').value;
      const wetVal = +byId('wet-raw').value;
      if (dryVal <= wetVal) {
        showMessage('Dry ADC reading must be greater than Wet ADC reading.', 'error');
        return;
      }
      try {
        await api('/api/calibration', 'POST', { dryRaw: dryVal, wetRaw: wetVal });
        showMessage('Probe calibration saved successfully.', 'success');
        refreshStatus();
      } catch (error) {
        showMessage(error.message, 'error');
      }
    });
  }

  // Save Cloud Settings
  const saveCloudBtn = byId('save-cloud');
  if (saveCloudBtn) {
    saveCloudBtn.addEventListener('click', async () => {
      const payload = {
        enabled: byId('cloud-enabled').checked,
        host: byId('cloud-host').value.trim(),
        port: +byId('cloud-port').value || 80,
        useSsl: byId('cloud-ssl').checked,
        deviceId: byId('cloud-device-id').value.trim(),
        telemetryIntervalMs: Math.max(5000, (+byId('cloud-interval').value || 30) * 1000),
      };
      const secret = byId('cloud-device-secret').value.trim();
      if (secret) payload.deviceSecret = secret;

      try {
        await api('/api/cloud', 'POST', payload);
        showMessage('Cloud configuration saved. Reconnecting...', 'success');
        setTimeout(loadCloudSettings, 1500);
      } catch (error) {
        showMessage(error.message, 'error');
      }
    });
  }
}

async function loadCloudSettings() {
  try {
    const cloud = await api('/api/cloud');
    const isEditing = document.activeElement && document.activeElement.id && document.activeElement.id.startsWith('cloud-');
    if (!isEditing) {
      if (byId('cloud-enabled')) byId('cloud-enabled').checked = !!cloud.enabled;
      if (byId('cloud-host')) byId('cloud-host').value = cloud.host || '';
      if (byId('cloud-port')) byId('cloud-port').value = cloud.port || 80;
      if (byId('cloud-ssl')) byId('cloud-ssl').checked = (cloud.port === 80) ? false : !!cloud.useSsl;
      if (byId('cloud-device-id')) byId('cloud-device-id').value = cloud.deviceId || '';
      if (byId('cloud-interval')) byId('cloud-interval').value = Math.round((cloud.telemetryIntervalMs || 30000) / 1000);
    }

    updateCloudBadges(cloud);
  } catch (error) {
    console.warn('Could not load cloud settings:', error);
  }
}

function updateCloudBadges(cloud) {
  const badge = byId('cloud-badge');
  const liveStatus = byId('cloud-live-status');

  if (!cloud.enabled) {
    if (badge) {
      badge.textContent = 'Disabled';
      badge.className = 'status-pill status-neutral';
    }
    if (liveStatus) {
      liveStatus.textContent = 'Cloud Link Disabled';
      liveStatus.style.color = 'var(--text-muted)';
    }
  } else if (cloud.connected) {
    if (badge) {
      badge.textContent = cloud.useSsl ? 'Connected (WSS)' : 'Connected (WS)';
      badge.className = 'status-pill status-active';
    }
    if (liveStatus) {
      liveStatus.textContent = 'Online -> ' + cloud.host;
      liveStatus.style.color = 'var(--plant-primary)';
    }
  } else {
    if (badge) {
      badge.textContent = 'Connecting...';
      badge.className = 'status-pill status-caution';
    }
    if (liveStatus) {
      liveStatus.textContent = 'Offline / Reconnecting to ' + (cloud.host || 'server');
      liveStatus.style.color = 'var(--alert-danger)';
    }
  }
}

// Initial Boot
document.addEventListener('DOMContentLoaded', () => {
  setupEventListeners();
  Promise.all([refreshStatus(), loadSettings(), loadCloudSettings()]).catch((err) => showMessage(err.message, 'error'));
  setInterval(refreshStatus, 2000);
  setInterval(loadCloudSettings, 6000);
});
