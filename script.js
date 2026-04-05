// Default Settings
const DEFAULT_SETTINGS = {
    espIp: "http://10.245.42.179",
    statusInterval: 800,
    stateInterval: 400
};

// Global State
let settings = { ...DEFAULT_SETTINGS };
let statusTimer = null;
let stateTimer = null;
let popupActive = false;
let currentVerifySlot = null;
let lastStatusRaw = "";

let logContainer = null;

// Helper to add log entries
function addLog(message, type = 'normal') {
    if (!logContainer) return;
    const time = new Date().toLocaleTimeString('en-US', { hour12: false, hour: "2-digit", minute: "2-digit", second: "2-digit" });
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    entry.innerHTML = `<span style="opacity:0.4">[${time}]</span> > ${message}`;
    logContainer.appendChild(entry);

    // Auto-scroll to bottom
    const parent = logContainer.parentElement;
    parent.scrollTop = parent.scrollHeight;

    // Maximum 50 logs to prevent DOM clutter
    if (logContainer.children.length > 50) {
        logContainer.removeChild(logContainer.firstChild);
    }
}

// ----------------------------------------------------
// UI Logic
// ----------------------------------------------------
function updateSlotUI(slotId, statusText) {
    const card = document.getElementById(`slot-${slotId}`);
    const badge = document.getElementById(`status-badge-${slotId}`);

    if (!card || !badge) return;

    if (statusText !== badge.innerText) {
        badge.innerText = statusText;
        // Apply CSS class based on state (e.g. 'free', 'verification-pending')
        let stateClass = statusText.toLowerCase().trim().replace(/\s+/g, '-');
        card.className = `slot-card ${stateClass}`;
    }
}

// ----------------------------------------------------
// Modal Implementation
// ----------------------------------------------------
function showVerifyModal(slotId) {
    if (popupActive) return; // Prevent duplicate Modals

    popupActive = true;
    currentVerifySlot = slotId;

    const modal = document.getElementById('verify-modal');
    const msg = document.getElementById('modal-message');

    msg.innerText = `Is It Your Vehicle IN SLOT 0${slotId}`;
    modal.classList.add('active');

    addLog(`System alert: Verification required for slot ${slotId}`, 'warning');
}

function hideVerifyModal() {
    const modal = document.getElementById('verify-modal');
    modal.classList.remove('active');

    setTimeout(() => {
        popupActive = false;
        currentVerifySlot = null;
    }, 300); // Wait for transition to complete
}

// Modal Action Buttons
function verifyAction() {
    if (currentVerifySlot !== null) {
        verifySlot(currentVerifySlot);
    }
    hideVerifyModal();
}

function declineAction() {
    if (currentVerifySlot !== null) {
        declineSlot(currentVerifySlot);
    }
    hideVerifyModal();
}

// ----------------------------------------------------
// Settings Management
// ----------------------------------------------------
function openSettings() {
    const modal = document.getElementById('settings-modal');
    document.getElementById('setting-esp-ip').value = settings.espIp;
    document.getElementById('setting-status-interval').value = settings.statusInterval;
    document.getElementById('setting-state-interval').value = settings.stateInterval;
    modal.classList.add('active');
}

function closeSettings() {
    const modal = document.getElementById('settings-modal');
    modal.classList.remove('active');
}

function saveSettings() {
    const newIp = document.getElementById('setting-esp-ip').value.trim();
    const newStatusInt = parseInt(document.getElementById('setting-status-interval').value);
    const newStateInt = parseInt(document.getElementById('setting-state-interval').value);

    if (!newIp || isNaN(newStatusInt) || isNaN(newStateInt)) {
        addLog("Invalid settings data. Changes not saved.", "error");
        return;
    }

    settings.espIp = newIp;
    settings.statusInterval = newStatusInt;
    settings.stateInterval = newStateInt;

    localStorage.setItem('parking_system_settings', JSON.stringify(settings));
    addLog("System configuration updated and saved.", "success");

    restartIntervals();
    closeSettings();
}

function resetSettings() {
    if (confirm("Reset current configuration to system defaults?")) {
        settings = { ...DEFAULT_SETTINGS };
        localStorage.removeItem('parking_system_settings');
        addLog("System settings restored to defaults.", "warning");
        restartIntervals();
        closeSettings();
    }
}

function loadSettings() {
    const saved = localStorage.getItem('parking_system_settings');
    if (saved) {
        try {
            settings = JSON.parse(saved);
        } catch (e) {
            console.error("Failed to parse saved settings", e);
        }
    }
}

function restartIntervals() {
    if (statusTimer) clearInterval(statusTimer);
    if (stateTimer) clearInterval(stateTimer);

    statusTimer = setInterval(pollStatus, settings.statusInterval);
    stateTimer = setInterval(pollState, settings.stateInterval);

    addLog(`Intervals restarted. Status: ${settings.statusInterval}ms | State: ${settings.stateInterval}ms`, "info");
}

// ----------------------------------------------------
// HTTP Endpoints (ESP32 Backend Communication)
// ----------------------------------------------------

function bookSlot(slotId) {
    addLog(`Initiating HTTP sequence -> /book?slot=${slotId}...`, 'info');

    fetch(`${settings.espIp}/book?slot=${slotId}`, {
  headers: {
    "ngrok-skip-browser-warning": "true"
  }
})
        .then(response => response.text())
        .then(text => {
            addLog(`Book response [Slot ${slotId}]: ${text}`, 'success');
        })
        .catch(error => {
            if (error.message.includes("Failed to fetch")) {
                addLog(`Signal sent [Slot ${slotId}]. (Response blocked by CORS, but hardware likely received it)`, 'warning');
            } else {
                addLog(`Book request error: ${error.message}`, 'error');
            }
        });
}

function verifySlot(slotId) {
    addLog(`Sending verification signal -> /verify?slot=${slotId}`, 'info');

    fetch(`${settings.espIp}/verify?slot=${slotId}`, {
  headers: {
    "ngrok-skip-browser-warning": "true"
  }
})
        .then(response => response.text())
        .then(text => addLog(`Verify response: ${text}`, 'success'))
        .catch(error => {
            if (error.message.includes("Failed to fetch")) {
                addLog(`Verify sent sent [Slot ${slotId}]. (No CORS header)`, 'warning');
            } else {
                addLog(`Verify failed: ${error.message}`, 'error');
            }
        });
}

function declineSlot(slotId) {
    addLog(`Sending override signal -> /decline?slot=${slotId}`, 'info');

    fetch(`${settings.espIp}/decline?slot=${slotId}`, {
  headers: {
    "ngrok-skip-browser-warning": "true"
  }
})
        .then(response => response.text())
        .then(text => addLog(`Decline response: ${text}`, 'success'))
        .catch(error => {
            if (error.message.includes("Failed to fetch")) {
                addLog(`Decline sent [Slot ${slotId}]. (No CORS header)`, 'warning');
            } else {
                addLog(`Decline failed: ${error.message}`, 'error');
            }
        });
}

// ----------------------------------------------------
// Status Polling
// ----------------------------------------------------
function pollStatus() {
    if (popupActive) return;

    fetch(`${settings.espIp}/status`, {
  headers: {
    "ngrok-skip-browser-warning": "true"
  }
})
        .then(response => response.text())
        .then(data => {
            // Target format expected: "VERIFY:i"
            if (data.startsWith('VERIFY:')) {
                const parts = data.split(':');
                if (parts.length > 1) {
                    const slot = parts[1].trim();
                    showVerifyModal(slot);
                }
            }
        })
        .catch(error => {
            // Un-comment the line below if you want to see network errors in the terminal
            // addLog(`Status poll error: ${error.message}`, 'error');
        });
}

// ----------------------------------------------------
// State Polling (UI Sync)
// ----------------------------------------------------
function pollState() {
    fetch(`${settings.espIp}/state`)
        .then(response => response.json())
        .then(data => {
            if (data && Array.isArray(data.slots)) {
                data.slots.forEach(slot => {
                    updateSlotUI(slot.id, slot.state);
                });
            }
        })
        .catch(error => {
            // Silent catch: harmless network misses during continuous sync
        });
}

// System Init
window.onload = () => {
    logContainer = document.getElementById('log-content');
    try {
        loadSettings();
        addLog("Neural link initialized. System ready.", 'success');
        addLog(`Target Uplink: ${settings.espIp}`, 'info');
        restartIntervals();
    } catch (e) {
        console.error("Initialization error:", e);
    }
};