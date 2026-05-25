#include "html.h"

const char HTML_CONTENT[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Neumann Volume Control</title>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #080c14;
            --panel-bg: rgba(17, 25, 40, 0.65);
            --border-color: rgba(255, 255, 255, 0.08);
            --accent-primary: #6366f1;
            --accent-secondary: #4f46e5;
            --accent-glow: rgba(99, 102, 241, 0.15);
            --text-main: #f3f4f6;
            --text-muted: #9ca3af;
            --green-glow: #10b981;
        }
        
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            user-select: none;
            -webkit-user-select: none;
        }
        
        body {
            font-family: 'Outfit', sans-serif;
            background-color: var(--bg-color);
            background-image: radial-gradient(circle at 50% -20%, #1e1b4b 0%, #080c14 70%);
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            padding: 20px;
            overflow-x: hidden;
        }
        
        .container {
            width: 100%;
            max-width: 480px;
            background: var(--panel-bg);
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
            border: 1px solid var(--border-color);
            border-radius: 24px;
            padding: 32px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.5), 0 0 50px var(--accent-glow);
            transition: all 0.3s ease;
            position: relative;
        }
        
        header {
            text-align: center;
            margin-bottom: 32px;
        }
        
        h1 {
            font-size: 1.8rem;
            font-weight: 800;
            letter-spacing: -0.05em;
            background: linear-gradient(to right, #a5b4fc, #6366f1);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            margin-bottom: 4px;
        }
        
        .subtitle {
            font-size: 0.85rem;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 0.1em;
            font-weight: 600;
        }
        
        .volume-display {
            text-align: center;
            margin-bottom: 24px;
        }
        
        .volume-value {
            font-size: 4.5rem;
            font-weight: 800;
            line-height: 1;
            letter-spacing: -0.03em;
            color: #ffffff;
            text-shadow: 0 0 20px rgba(255, 255, 255, 0.1);
            display: inline-block;
        }
        
        .volume-unit {
            font-size: 1.5rem;
            font-weight: 600;
            color: var(--text-muted);
            margin-left: 4px;
        }
        
        .slider-container {
            position: relative;
            margin-bottom: 32px;
            padding: 10px 0;
        }
        
        input[type="range"] {
            -webkit-appearance: none;
            width: 100%;
            height: 8px;
            border-radius: 4px;
            background: rgba(255, 255, 255, 0.1);
            outline: none;
            transition: background 450ms ease-in;
        }
        
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 28px;
            height: 28px;
            border-radius: 50%;
            background: #ffffff;
            box-shadow: 0 0 15px var(--accent-primary), 0 0 5px rgba(0,0,0,0.3);
            cursor: pointer;
            transition: transform 0.1s ease, background-color 0.1s ease;
            border: 2px solid var(--accent-primary);
        }
        
        input[type="range"]::-webkit-slider-thumb:hover {
            transform: scale(1.15);
            background: #f3f4f6;
        }
        
        input[type="range"]::-webkit-slider-thumb:active {
            transform: scale(0.95);
        }
        
        .presets {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 12px;
            margin-bottom: 32px;
        }
        
        .btn-preset {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            color: var(--text-main);
            padding: 12px 8px;
            border-radius: 12px;
            font-weight: 600;
            font-size: 0.9rem;
            cursor: pointer;
            transition: all 0.2s ease;
        }
        
        .btn-preset:hover {
            background: var(--accent-primary);
            border-color: var(--accent-primary);
            box-shadow: 0 0 15px rgba(99, 102, 241, 0.4);
            transform: translateY(-2px);
        }
        
        .btn-preset:active {
            transform: translateY(0);
        }
        
        .status-panel {
            background: rgba(0, 0, 0, 0.2);
            border-radius: 16px;
            padding: 16px;
            border: 1px solid rgba(255, 255, 255, 0.03);
        }
        
        .status-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 12px;
            font-size: 0.85rem;
            font-weight: 600;
            color: var(--text-muted);
        }
        
        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background-color: #ef4444;
            display: inline-block;
            margin-right: 6px;
            box-shadow: 0 0 8px #ef4444;
        }
        
        .status-dot.online {
            background-color: var(--green-glow);
            box-shadow: 0 0 8px var(--green-glow);
            animation: pulse 2s infinite;
        }
        
        @keyframes pulse {
            0% { opacity: 0.6; }
            50% { opacity: 1; }
            100% { opacity: 0.6; }
        }
        
        .speaker-list {
            list-style: none;
            font-size: 0.8rem;
            color: var(--text-muted);
        }
        
        .speaker-item {
            display: flex;
            justify-content: space-between;
            padding: 6px 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.03);
        }
        
        .speaker-item:last-child {
            border-bottom: none;
        }
        
        .error-overlay {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(8, 12, 20, 0.95);
            border-radius: 24px;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            opacity: 0;
            pointer-events: none;
            transition: opacity 0.4s ease;
            z-index: 10;
            padding: 32px;
            text-align: center;
        }
        
        .container.overlay-active .error-overlay {
            opacity: 1;
            pointer-events: auto;
        }
        
        .error-title {
            font-size: 1.3rem;
            font-weight: 700;
            margin-bottom: 8px;
            transition: color 0.3s ease;
        }
        
        .container.state-connecting .error-title {
            color: #a5b4fc;
        }
        .container.state-connecting .spinner {
            display: block;
            border-top-color: var(--accent-primary);
        }
        
        .container.state-offline .error-title {
            color: var(--text-muted);
        }
        .container.state-offline .spinner {
            display: none;
        }
        
        .container.state-error .error-title {
            color: #ef4444;
        }
        .container.state-error .spinner {
            display: block;
            border-top-color: #ef4444;
        }
        
        .spinner {
            width: 40px;
            height: 40px;
            border: 4px solid rgba(255, 255, 255, 0.1);
            border-radius: 50%;
            animation: spin 1s infinite linear;
            margin-bottom: 16px;
        }
        
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <div class="container overlay-active state-connecting" id="container">
        <div class="error-overlay" id="errorOverlay">
            <div class="spinner"></div>
            <div class="error-title" id="errorTitle">Scanning Network</div>
            <p style="color: var(--text-muted); font-size: 0.85rem;" id="errorDesc">Searching for Neumann monitors on the link-local network...</p>
        </div>
        
        <header>
            <h1>Neumann KH Control</h1>
            <div class="subtitle">Studio Monitor Alignment</div>
        </header>
        
        <div class="volume-display">
            <span class="volume-value" id="volVal">--.-</span>
            <span class="volume-unit">dB</span>
        </div>
        
        <div class="slider-container">
            <input type="range" id="volSlider" min="0" max="80" step="0.5" value="0" disabled>
        </div>
        
        <div class="presets">
            <button class="btn-preset" data-preset="30">30 dB</button>
            <button class="btn-preset" data-preset="50">50 dB</button>
            <button class="btn-preset" data-preset="65">65 dB</button>
            <button class="btn-preset" data-preset="75">75 dB</button>
        </div>
        
        <div class="status-panel">
            <div class="status-header">
                <span>MONITOR STATUS</span>
                <div>
                    <span class="status-dot" id="statusDot"></span>
                    <span id="statusText">OFFLINE</span>
                </div>
            </div>
            <ul class="speaker-list" id="speakerList">
                <li style="text-align: center; padding: 10px 0;">No discovered speakers</li>
            </ul>
        </div>
    </div>

    <script>
        const volVal = document.getElementById('volVal');
        const volSlider = document.getElementById('volSlider');
        const container = document.getElementById('container');
        const statusDot = document.getElementById('statusDot');
        const statusText = document.getElementById('statusText');
        const speakerList = document.getElementById('speakerList');
        const errorOverlay = document.getElementById('errorOverlay');
        const errorTitle = document.getElementById('errorTitle');
        
        let updateTimeout = null;
        let isDragging = false;

        function showOverlay(state, title, desc) {
            container.className = `container overlay-active state-${state}`;
            errorTitle.innerText = title;
            document.getElementById('errorDesc').innerText = desc;
            volSlider.disabled = true;
        }

        function hideOverlay() {
            container.className = 'container';
            volSlider.disabled = false;
        }

        async function fetchState() {
            try {
                // Fetch speakers list
                const spRes = await fetch('/api/speakers');
                const spData = await spRes.json();
                
                // Fetch current volume level
                const lvlRes = await fetch('/api/level');
                
                if (lvlRes.status === 503) {
                    showOverlay("offline", "Monitors Offline", "Ensure Neumann monitors are powered on and connected to the Ethernet port.");
                    updateSpeakerUI(spData.speakers || []);
                    return;
                }
                
                if (!lvlRes.ok) throw new Error("Failed to fetch level");
                
                const lvlData = await lvlRes.json();
                
                hideOverlay();
                updateSpeakerUI(spData.speakers || []);
                
                if (!isDragging) {
                    volVal.innerText = lvlData.level.toFixed(1);
                    volSlider.value = lvlData.level;
                }
            } catch (err) {
                showOverlay("error", "Controller Unreachable", "Unable to communicate with the ESP32 volume controller. Retrying...");
                console.error(err);
            }
        }

        function updateSpeakerUI(speakers) {
            if (speakers.length > 0) {
                statusDot.className = 'status-dot online';
                statusText.innerText = `CONNECTED (${speakers.length})`;
                speakerList.innerHTML = speakers.map((s, idx) => `
                    <li class="speaker-item">
                        <span>Monitor ${idx + 1}</span>
                        <span>${s.ip}</span>
                    </li>
                `).join('');
            } else {
                statusDot.className = 'status-dot';
                statusText.innerText = "OFFLINE";
                speakerList.innerHTML = '<li style="text-align: center; padding: 10px 0;">No discovered speakers</li>';
            }
        }

        async function setVolume(level) {
            try {
                const res = await fetch('/api/level', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ level })
                });
                const data = await res.json();
                volVal.innerText = data.level.toFixed(1);
                if (!isDragging) {
                    volSlider.value = data.level;
                }
            } catch (err) {
                console.error("Error setting volume:", err);
            }
        }

        volSlider.addEventListener('input', (e) => {
            isDragging = true;
            volVal.innerText = parseFloat(e.target.value).toFixed(1);
            
            // Debounce API calls while sliding
            if (updateTimeout) clearTimeout(updateTimeout);
            updateTimeout = setTimeout(() => {
                setVolume(parseFloat(e.target.value));
            }, 50);
        });

        volSlider.addEventListener('change', (e) => {
            isDragging = false;
            setVolume(parseFloat(e.target.value));
        });

        function setVolumePreset(level) {
            volSlider.value = level;
            volVal.innerText = level.toFixed(1);
            setVolume(level);
        }

        // Attach preset button listeners
        document.querySelectorAll('.btn-preset').forEach(btn => {
            btn.addEventListener('click', () => {
                setVolumePreset(parseFloat(btn.dataset.preset));
            });
        });

        let pollTimer = null;

        async function fetchState() {
            try {
                // Fetch speakers list
                const spRes = await fetch('/api/speakers');
                const spData = await spRes.json();
                
                // Fetch current volume level
                const lvlRes = await fetch('/api/level');
                
                if (lvlRes.status === 503) {
                    showOverlay("offline", "Monitors Offline", "Ensure Neumann monitors are powered on and connected to the Ethernet port.");
                    updateSpeakerUI(spData.speakers || []);
                    setNextPoll(1000); // Poll faster (1s) when offline
                    return;
                }
                
                if (!lvlRes.ok) throw new Error("Failed to fetch level");
                
                const lvlData = await lvlRes.json();
                
                hideOverlay();
                updateSpeakerUI(spData.speakers || []);
                
                if (!isDragging) {
                    volVal.innerText = lvlData.level.toFixed(1);
                    volSlider.value = lvlData.level;
                }
                setNextPoll(3000); // Standard poll (3s) when online
            } catch (err) {
                showOverlay("error", "Controller Unreachable", "Unable to communicate with the ESP32 volume controller. Retrying...");
                console.error(err);
                setNextPoll(2000); // Moderate poll (2s) on error
            }
        }

        function setNextPoll(delay) {
            if (pollTimer) clearTimeout(pollTimer);
            pollTimer = setTimeout(fetchState, delay);
        }

        // Initial load starts the cycle
        fetchState();
    </script>
</body>
</html>
)rawliteral";
