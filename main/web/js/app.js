// Main application logic
function switchTab(tabName) {
    // Stop monitor polling if switching away from monitor tab
    if (tabName !== 'monitor' && monitorPollInterval) {
        clearInterval(monitorPollInterval);
        monitorPollInterval = null;
    }

    // Hide all tab contents
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });

    // Deactivate all tabs
    document.querySelectorAll('.tab').forEach(tab => {
        tab.classList.remove('active');
    });

    // Activate current tab
    const currentTab = document.querySelector(`.tab[onclick="switchTab('${tabName}')"]`);
    currentTab.classList.add('active');

    // Show current tab content
    const tabContent = document.getElementById(tabName);
    tabContent.classList.add('active');

    // Load content for the specific tab
    loadTabContent(tabName);
}

function loadTabContent(tabName) {
    const tabContent = document.getElementById(tabName);
    fetch(`tabs/${tabName}.html`)
        .then(response => response.text())
        .then(html => {
            tabContent.innerHTML = html;

            // Run specific initialization for each tab
            switch(tabName) {
                case 'monitor':
                    initMonitorTab();
                    break;
                case 'signals':
                    initSignalsTab();
                    break;
                case 'manual':
                    initManualTab();
                    break;
                case 'settings':
                    initSettingsTab();
                    break;
                case 'api':
                    initApiTab();
                    break;
            }
        });
}

let monitorPollInterval = null;

function initMonitorTab() {
    // Start periodic polling for new signals
    if (monitorPollInterval) {
        clearInterval(monitorPollInterval);
    }
    pollLastSignal(); // Initial poll
    monitorPollInterval = setInterval(() => {
        pollLastSignal();
    }, 1000); // Poll every 1 second
}

function initSignalsTab() {
    fetchSavedSignals();
}

function initManualTab() {
    // Manual tab specific initialization if needed
}

function initSettingsTab() {
    loadSettings();
}

function initApiTab() {
    // API tab specific initialization if needed
}

// Initialization on page load
document.addEventListener('DOMContentLoaded', () => {
    // Set initial active tab
    switchTab('monitor');
});