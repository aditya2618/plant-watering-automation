const form = document.getElementById('provision-form');
const message = document.getElementById('message');

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  const password = document.getElementById('dashboard-password').value;
  const confirmPassword = document.getElementById('dashboard-password-confirm').value;

  if (password !== confirmPassword) {
    message.textContent = 'Dashboard passwords do not match.';
    message.className = 'message-text error';
    return;
  }

  const button = form.querySelector('button[type="submit"]');
  button.disabled = true;
  message.className = 'message-text';
  message.textContent = 'Saving configuration and testing network...';

  try {
    const payload = {
      wifiSsid: document.getElementById('wifi-ssid').value,
      wifiPassword: document.getElementById('wifi-password').value,
      dashboardPassword: password,
    };

    const response = await fetch('/api/provision', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });

    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Provisioning failed');

    message.className = 'message-text success';
    message.textContent = 'Saved! Device is now rebooting and connecting to your home Wi-Fi.';
  } catch (error) {
    message.className = 'message-text error';
    message.textContent = error.message;
    button.disabled = false;
  }
});
