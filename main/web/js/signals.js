// Global variables
let lastSignal = null;
let savedSignals = [];
let signalMap = {};

function displaySignal(signal, shouldFlash = false) {
    const display = document.getElementById('signalDisplay');
    
    // If signalMap is already populated from API, just refresh the display
    if (signalMap._order && signalMap._order.length > 0) {
        const signalsList = signalMap._order.map(key => ({
            key,
            ...signalMap[key]
        }));
        
        // Update relative times for display
        // Account for elapsed time since we received the server time
        const now = Date.now() * 1000; // Current time in microseconds
        const elapsedSinceReceive = signalMap._receivedAt ? (now - signalMap._receivedAt) : 0;
        const currentServerTime = (signalMap._serverTime || now) + elapsedSinceReceive;
        
        const consoleHTML = signalsList.map(item => {
            const sig = item.signal;
            const hexCode = '0x' + sig.code.toString(16).toUpperCase().padStart(6, '0');
            const itemKey = `${sig.code}_${sig.bitLength}_${sig.protocol}`;
            const shouldFlashThis = false; // Flash is handled by new signal detection
            
            // Recalculate relative time on each display
            const timeAgo = item.lastSeenTimestamp ? formatTimeAgo(currentServerTime, item.lastSeenTimestamp) : item.lastUpdate;

        return `
            <div class="console-line ${shouldFlashThis ? 'flash' : ''}">
                <span class="console-time">${timeAgo}</span>
                <span class="console-count">[×${item.count.toString().padStart(3, ' ')}]</span>
                <span class="console-code">${sig.code}</span>
                <span class="console-hex">${hexCode}</span>
                <span class="console-meta">
                    <span class="meta-bits">bits=${sig.bitLength}</span>
                    <span class="meta-proto">proto=${sig.protocol}</span>
                    <span class="meta-pulse">pulse=${sig.pulseLength}µs</span>
                </span>
                <div class="console-actions">
                    <button class="console-btn" onclick="replaySignal(${sig.code}, ${sig.bitLength}, ${sig.protocol}, ${sig.pulseLength})">Replay</button>
                    <button class="console-btn console-btn-save" onclick="quickSaveSignal(${sig.code}, ${sig.bitLength}, ${sig.protocol}, ${sig.pulseLength})">Save</button>
                </div>
            </div>
        `;
        }).join('');

        display.innerHTML = consoleHTML || '<div class="empty-state">Waiting for RF signals...</div>';
        return;
    }

    // Fallback: if signalMap is empty, show empty state
    display.innerHTML = '<div class="empty-state">Waiting for RF signals...</div>';
}

function updateLastSignalDisplay() {
    if (lastSignal) {
        const hexCode = '0x' + lastSignal.code.toString(16).toUpperCase();
        document.getElementById('lastCode').textContent = lastSignal.code + ' (' + hexCode + ')';
        document.getElementById('lastBits').textContent = lastSignal.bitLength;
        document.getElementById('lastProtocol').textContent = lastSignal.protocol;
        document.getElementById('lastPulse').textContent = lastSignal.pulseLength + 'µs';
    }
}

function displaySavedSignals() {
    const container = document.getElementById('savedSignals');

    if (savedSignals.length === 0) {
        container.innerHTML = '<div class="empty-state">No saved signals</div>';
        return;
    }

    // Check if signals have actually changed
    const signalsChanged =
        savedSignals.length !== container.children.length ||
        Array.from(container.children).some((el, index) => {
            const signal = savedSignals[index];
            return !signal ||
                el.querySelector('h3').textContent !== signal.name ||
                el.querySelector('.signal-meta:first-of-type').textContent !== `Code: ${signal.code} (0x${signal.code.toString(16).toUpperCase()})`;
        });

    // Only update if there are actual changes
    if (signalsChanged) {
        // Generate new HTML
        const signalsHTML = savedSignals.map((signal, index) => `
            <div class="signal-card" data-signal-index="${index}">
                <h3>${signal.name}</h3>
                <div class="signal-meta">Code: ${signal.code} (0x${signal.code.toString(16).toUpperCase()})</div>
                <div class="signal-meta">Bits: ${signal.bitLength} | Protocol: ${signal.protocol}</div>
                <div class="button-group">
                    <button class="success" onclick="transmitSignal(${index})">Send</button>
                    <button onclick="editSignal(${index})">Edit</button>
                    <button class="danger" onclick="deleteSignal(${index})">Delete</button>
                </div>
            </div>
        `).join('');

        container.innerHTML = signalsHTML;
    }
}

function quickSaveSignal(code, bitLength, protocol, pulseLength) {
    const name = prompt('Signal name:');
    if (!name || !name.trim()) return;

    fetch('/api/signals', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
            name: name.trim(),
            code: code,
            bitLength: bitLength,
            protocol: protocol,
            pulseLength: pulseLength
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
    });
}

function replaySignal(code, bitLength, protocol, pulseLength) {
    // Transmit directly without saving
    fetch('/api/transmit', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
            code: code,
            bitLength: bitLength,
            protocol: protocol,
            pulseLength: pulseLength
        })
    })
    .then(response => {
        if (!response.ok) {
            return response.json().then(err => Promise.reject(err));
        }
        return response.json();
    })
    .then(() => {
        alert('Signal replayed successfully!');
    })
    .catch(error => {
        const errorMsg = error.error || error.message || 'Failed to replay signal';
        alert('Error: ' + errorMsg);
    });
}

function editSignal(index) {
    const signal = savedSignals[index];

    const name = prompt('Name:', signal.name);
    if (!name || !name.trim()) return;

    const codeStr = prompt('Code:', signal.code);
    if (!codeStr) return;
    const code = codeStr.startsWith('0x') ? parseInt(codeStr, 16) : parseInt(codeStr, 10);

    const bitLength = parseInt(prompt('Bits:', signal.bitLength));
    const protocol = parseInt(prompt('Protocol:', signal.protocol));
    const pulseLength = parseInt(prompt('Pulse (µs):', signal.pulseLength));

    if (isNaN(code) || isNaN(bitLength) || isNaN(protocol) || isNaN(pulseLength)) return;

    fetch('/api/signals/' + index, {
        method: 'PUT',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
            name: name.trim(),
            code: code,
            bitLength: bitLength,
            protocol: protocol,
            pulseLength: pulseLength
        })
    })
    .then(r => r.json())
    .then(data => {
        if (data.success) fetchSavedSignals();
    });
}