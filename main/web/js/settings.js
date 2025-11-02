// Settings management
let currentSettings = {
    noiseFiltering: false,
    noiseThreshold: 3,
    monitorInterval: 200,
    maxSignalHistory: 15,
    defaultProtocol: 2,
    defaultPulseLength: 650
};

function loadSettings() {
    const savedSettings = localStorage.getItem('esp32_rf_settings');
    if (savedSettings) {
        currentSettings = JSON.parse(savedSettings);
        applySettings();
    }
}

function saveSettings() {
    // Collect settings from UI
    currentSettings.noiseFiltering = document.getElementById('noiseFilterToggle').checked;
    currentSettings.noiseThreshold = parseInt(document.getElementById('noiseThreshold').value);
    currentSettings.monitorInterval = parseInt(document.getElementById('monitorInterval').value);
    currentSettings.maxSignalHistory = parseInt(document.getElementById('maxSignalHistory').value);
    currentSettings.defaultProtocol = parseInt(document.getElementById('defaultProtocol').value);
    currentSettings.defaultPulseLength = parseInt(document.getElementById('defaultPulseLength').value);

    // Save to localStorage
    localStorage.setItem('esp32_rf_settings', JSON.stringify(currentSettings));

    // Apply settings
    applySettings();

    // Send configuration to ESP32 via API
    fetch('/api/settings', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(currentSettings)
    })
    .then(response => {
        if (!response.ok) {
            return response.json().then(err => Promise.reject(err));
        }
        return response.json();
    })
    .then(data => {
        if (data && data.success) {
            alert('Settings saved successfully!');
        } else {
            alert('Settings saved locally, but server response was unexpected.');
        }
    })
    .catch(error => {
        const errorMsg = error.error || error.message || 'Failed to save settings to server';
        alert('Warning: Settings saved locally, but ' + errorMsg);
    });
}

function applySettings() {
    // Apply noise filtering settings
    document.getElementById('noiseFilterToggle').checked = currentSettings.noiseFiltering;
    document.getElementById('noiseThreshold').value = currentSettings.noiseThreshold;

    // Apply monitoring settings
    document.getElementById('monitorInterval').value = currentSettings.monitorInterval;
    document.getElementById('maxSignalHistory').value = currentSettings.maxSignalHistory;

    // Apply transmission settings
    document.getElementById('defaultProtocol').value = currentSettings.defaultProtocol;
    document.getElementById('defaultPulseLength').value = currentSettings.defaultPulseLength;
}

function exportConfig() {
    const configStr = JSON.stringify(currentSettings, null, 2);
    const blob = new Blob([configStr], {type: 'application/json'});
    const url = URL.createObjectURL(blob);

    const a = document.createElement('a');
    a.href = url;
    a.download = 'esp32_rf_config.json';
    a.click();

    URL.revokeObjectURL(url);
}

function importConfig() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';

    input.onchange = (e) => {
        const file = e.target.files[0];
        const reader = new FileReader();

        reader.onload = (event) => {
            try {
                const importedSettings = JSON.parse(event.target.result);

                // Validate imported settings
                if (importedSettings && typeof importedSettings === 'object') {
                    currentSettings = {...currentSettings, ...importedSettings};

                    // Save to localStorage
                    localStorage.setItem('esp32_rf_settings', JSON.stringify(currentSettings));

                    // Apply new settings
                    applySettings();

                    alert('Configuration imported successfully!');
                } else {
                    throw new Error('Invalid configuration');
                }
            } catch (err) {
                alert('Error importing configuration: ' + err.message);
            }
        };

        reader.readAsText(file);
    };

    input.click();
}

function resetToDefaults() {
    if (!confirm('Reset all settings to default values?')) return;

    // Reset settings to defaults
    currentSettings = {
        noiseFiltering: false,
        noiseThreshold: 3,
        monitorInterval: 200,
        maxSignalHistory: 15,
        defaultProtocol: 2,
        defaultPulseLength: 650
    };

    // Remove from localStorage
    localStorage.removeItem('esp32_rf_settings');

    // Apply and display default settings
    applySettings();

    // Inform user
    alert('Settings reset to default values.');
}