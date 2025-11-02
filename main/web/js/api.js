// API interaction functions
function fetchWithErrorHandling(url, options = {}) {
    return fetch(url, options)
        .then(response => {
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            return response.json();
        })
        .catch(error => {
            console.error(`Error fetching ${url}:`, error);
            return null;
        });
}

// Signal-related API calls
function fetchSavedSignals() {
    return fetchWithErrorHandling('/api/signals')
        .then(data => {
            if (data && data.signals) {
                savedSignals = data.signals;
                displaySavedSignals();
            }
        });
}

function pollLastSignal() {
    return fetchWithErrorHandling('/api/signal-history')
        .then(data => {
            if (!data) return;
            
            // Update signal history from tracked signals
            if (data.signals && Array.isArray(data.signals)) {
                // Store server time and when we received it
                const receivedAt = Date.now() * 1000; // microseconds
                const serverTime = data.serverTime || receivedAt;
                
                // Clear and rebuild signal map from server data
                signalMap = {};
                signalMap._order = [];
                signalMap._serverTime = serverTime;
                signalMap._receivedAt = receivedAt;
                
                // Sort by firstSeen to maintain detection order
                const sortedSignals = [...data.signals].sort((a, b) => a.firstSeen - b.firstSeen);
                
                sortedSignals.forEach((sig, index) => {
                    const signalKey = `${sig.code}_${sig.bitLength}_${sig.protocol}`;
                    // ESP32 timestamps are in microseconds since boot
                    // Calculate relative time using serverTime and lastSeen
                    const timeAgo = formatTimeAgo(signalMap._serverTime, sig.lastSeen);
                    
                    signalMap[signalKey] = {
                        signal: {
                            code: sig.code,
                            bitLength: sig.bitLength,
                            protocol: sig.protocol,
                            pulseLength: sig.pulseLength
                        },
                        count: sig.count,
                        lastUpdate: timeAgo,
                        lastSeenTimestamp: sig.lastSeen,
                        firstDetected: sig.firstSeen,
                        firstDetectedTime: formatTimeAgo(signalMap._serverTime, sig.firstSeen)
                    };
                    
                    if (!signalMap._order.includes(signalKey)) {
                        signalMap._order.push(signalKey);
                    }
                });
                
                // Update display with all signals
                displaySignal(null, false);
            }
            
            // Update latest signal display
            if (data.latest && data.latest.new && data.latest.code) {
                lastSignal = data.latest;
                updateLastSignalDisplay();
            } else if (data.latest && !data.latest.new && lastSignal && lastSignal.code) {
                // Keep displaying last known signal
                updateLastSignalDisplay();
            }
        });
}

function saveCurrentSignal(name) {
    if (!lastSignal || !lastSignal.code) {
        alert('No signal received yet');
        return Promise.reject('No signal');
    }

    return fetch('/api/signals', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
            name: name,
            code: lastSignal.code,
            bitLength: lastSignal.bitLength,
            protocol: lastSignal.protocol,
            pulseLength: lastSignal.pulseLength
        })
    })
    .then(response => {
        return response.json().then(data => {
            if (!response.ok) {
                return Promise.reject(data);
            }
            return data;
        });
    })
    .then(data => {
        if (data && data.success) {
            fetchSavedSignals();
            alert('Signal saved successfully!');
        } else {
            const responseText = JSON.stringify(data, null, 2);
            alert('Response: ' + responseText);
        }
        return data;
    })
    .catch(error => {
        const errorMsg = error.error || error.message || JSON.stringify(error);
        alert('Error: ' + errorMsg);
        throw error;
    });
}

function transmitSignal(index) {
    return fetchWithErrorHandling(`/api/transmit/${index}`, {
        method: 'POST'
    });
}

function deleteSignal(index) {
    return fetchWithErrorHandling(`/api/signals/${index}`, {
        method: 'DELETE'
    }).then(data => {
        if (data && data.success) {
            fetchSavedSignals();
        }
        return data;
    });
}

function clearTracking() {
    return fetchWithErrorHandling('/api/clear-tracking', {
        method: 'POST'
    }).then(data => {
        if (data && data.success) {
            signalMap = {};
            signalMap._order = [];
            document.getElementById('signalDisplay').innerHTML = '<div class="empty-state">Waiting for RF signals...</div>';
        }
        return data;
    });
}

function formatTimeAgo(serverTime, timestamp) {
    // Both times are in microseconds
    const diff = serverTime - timestamp;
    const diffSeconds = Math.floor(diff / 1000000);
    
    if (diffSeconds < 60) {
        return `${diffSeconds}s ago`;
    } else if (diffSeconds < 3600) {
        const minutes = Math.floor(diffSeconds / 60);
        const seconds = diffSeconds % 60;
        if (seconds > 0) {
            return `${minutes}m ${seconds}s ago`;
        }
        return `${minutes}m ago`;
    } else if (diffSeconds < 86400) {
        const hours = Math.floor(diffSeconds / 3600);
        const minutes = Math.floor((diffSeconds % 3600) / 60);
        if (minutes > 0) {
            return `${hours}h ${minutes}m ago`;
        }
        return `${hours}h ago`;
    } else {
        const days = Math.floor(diffSeconds / 86400);
        return `${days}d ago`;
    }
}

function saveCurrentSignalFromInput() {
    const nameInput = document.getElementById('signalName');
    if (!nameInput) {
        alert('Signal name input not found');
        return;
    }
    
    const name = nameInput.value.trim();
    if (!name) {
        alert('Please enter a signal name');
        return;
    }
    
    saveCurrentSignal(name)
        .then(() => {
            // Clear the input on success
            nameInput.value = '';
        })
        .catch(() => {
            // Error already shown by saveCurrentSignal
        });
}