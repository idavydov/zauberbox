const API_BASE = '/api';
const COVER_MAX_DIMENSION = 1600;
const COVER_JPEG_QUALITIES = [0.82, 0.72, 0.62];
const COVER_TARGET_MAX_BYTES = 900 * 1024;
const PRINT_DPI = 300;
const MM_TO_INCH = 25.4;
const TILE_WIDTH_MM = 100;
const TILE_HEIGHT_MM = 50;
const SHEET_ROWS = 3;
let state = {
    view: 'dashboard',
    currentPath: null,
    directories: [],
    files: [],
    deviceStatus: null,
    deviceStatusError: '',
    loading: true,
    upload: null,
    debugPreviewUrl: null,
    debugPreviewLoading: false,
    debugPreviewError: '',
    debugPreviewUpdatedAt: 0,
    debugPreviewSessionActive: false,
    debugCameraKernel: false,
    debugLogsText: '',
    debugLogsLoading: false,
    debugLogsError: '',
    debugLogsUpdatedAt: 0,
    debugBatteryError: '',
    selectionMode: false,
    selectedDirs: new Set(),
    error: null
};
let debugPreviewTimerId = 0;
let debugPreviewAbortController = null;
let debugLogsTimerId = 0;
let debugLogsAbortController = null;
let deviceStatusTimerId = 0;
let deviceStatusAbortController = null;
let debugReturnHash = '#/';

const MOCK_DATA = [
    { name: "001", cover: null, first_mp3: "01_intro.mp3" },
    { name: "002", cover: "https://picsum.photos/seed/2/400", first_mp3: "fairytale_part1.mp3" },
    { name: "003", cover: "https://picsum.photos/seed/3/400", first_mp3: "bedtime_story.mp3" }
];

/**
 * Lucide Icons (ISC License)
 * https://lucide.dev/
 */
const ICONS = {
    music: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-music-icon lucide-music"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>`,
    edit: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-square-pen-icon lucide-square-pen"><path d="M12 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.375 2.625a1 1 0 0 1 3 3l-9.013 9.014a2 2 0 0 1-.853.505l-2.873.84a.5.5 0 0 1-.62-.62l.84-2.873a2 2 0 0 1 .506-.852z"/></svg>`,
    trash: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-trash2-icon lucide-trash-2"><path d="M10 11v6"/><path d="M14 11v6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M3 6h18"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>`,
    addFolder: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-folder-plus-icon lucide-folder-plus"><path d="M12 10v6"/><path d="M9 13h6"/><path d="M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z"/></svg>`,
    addFile: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-file-plus-icon lucide-file-plus"><path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z"/><path d="M14 2v5a1 1 0 0 0 1 1h5"/><path d="M9 15h6"/><path d="M12 18v-6"/></svg>`,
    back: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-arrow-left-icon lucide-arrow-left"><path d="m12 19-7-7 7-7"/><path d="M19 12H5"/></svg>`,
    image: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-image-icon lucide-image"><rect width="18" height="18" x="3" y="3" rx="2" ry="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"/></svg>`,
    file: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-file-icon lucide-file"><path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z"/><path d="M14 2v5a1 1 0 0 0 1 1h5"/></svg>`,
    download: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-download-icon lucide-download"><path d="M12 15V3"/><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m7 10 5 5 5-5"/></svg>`,
    check: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-check-icon lucide-check"><path d="M20 6 9 17l-5-5"/></svg>`,
    menu: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-menu-icon lucide-menu"><path d="M4 5h16"/><path d="M4 12h16"/><path d="M4 19h16"/></svg>`,
    info: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-info-icon lucide-info"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/></svg>`,
    batteryFull: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-battery-full-icon lucide-battery-full"><path d="M10 10v4"/><path d="M14 10v4"/><path d="M22 14v-4"/><path d="M6 10v4"/><rect x="2" y="6" width="16" height="12" rx="2"/></svg>`,
    batteryMedium: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-battery-medium-icon lucide-battery-medium"><path d="M10 14v-4"/><path d="M22 14v-4"/><path d="M6 14v-4"/><rect x="2" y="6" width="16" height="12" rx="2"/></svg>`,
    batteryLow: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-battery-low-icon lucide-battery-low"><path d="M22 14v-4"/><path d="M6 14v-4"/><rect x="2" y="6" width="16" height="12" rx="2"/></svg>`,
    batteryEmpty: `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-battery-icon lucide-battery"><path d="M 22 14 L 22 10"/><rect x="2" y="6" width="16" height="12" rx="2"/></svg>`
};

function batteryIconName() {
    const battery = state.deviceStatus?.battery;
    if (!battery?.initialized || !battery?.reading_available || !battery?.has_reading) {
        return 'batteryEmpty';
    }
    if (!battery?.reading_stable || battery?.availability === 'settling') {
        return 'batteryEmpty';
    }
    if (battery?.is_critical || battery?.percent <= 10) {
        return 'batteryEmpty';
    }
    if (battery?.is_low || battery?.percent <= 30) {
        return 'batteryLow';
    }
    if (battery?.percent <= 75) {
        return 'batteryMedium';
    }
    return 'batteryFull';
}

function toggleSelectionMode() {
    state.selectionMode = !state.selectionMode;
    if (!state.selectionMode) state.selectedDirs.clear();
    render();
}

function toggleDirSelection(name) {
    if (state.selectedDirs.has(name)) {
        state.selectedDirs.delete(name);
    } else {
        state.selectedDirs.add(name);
    }
    render();
}

function showModal({ title, message, input = false, value = '', confirmText = 'Confirm', cancelText = 'Cancel', showCancel = true }) {
    return new Promise((resolve) => {
        const modal = document.getElementById('modal');
        const titleEl = document.getElementById('modal-title');
        const messageEl = document.getElementById('modal-message');
        const inputEl = document.getElementById('modal-input');
        const confirmBtn = document.getElementById('modal-confirm');
        const cancelBtn = document.getElementById('modal-cancel');
        const closeBtn = modal.querySelector('[aria-label="Close"]');

        titleEl.textContent = title;
        messageEl.textContent = message;
        confirmBtn.textContent = confirmText;
        cancelBtn.textContent = cancelText;
        cancelBtn.style.display = showCancel ? 'inline-block' : 'none';

        if (input) {
            inputEl.style.display = 'block';
            inputEl.value = value;
        } else {
            inputEl.style.display = 'none';
        }

        const cleanup = () => {
            confirmBtn.onclick = null;
            cancelBtn.onclick = null;
            closeBtn.onclick = null;
            inputEl.onkeydown = null;
            modal.close();
        };

        const handleConfirm = () => {
            const result = input ? inputEl.value : true;
            cleanup();
            resolve(result);
        };

        const handleCancel = () => {
            cleanup();
            resolve(null);
        };

        confirmBtn.onclick = handleConfirm;
        cancelBtn.onclick = handleCancel;
        closeBtn.onclick = handleCancel;
        inputEl.onkeydown = (e) => { if (e.key === 'Enter') handleConfirm(); };
        
        modal.showModal();
        if (input) setTimeout(() => inputEl.focus(), 50);
    });
}

async function fetchAPI(endpoint, options = {}) {
    const response = await fetch(`${API_BASE}${endpoint}`, options);
    if (!response.ok) {
        const errData = await response.json().catch(() => null);
        throw new Error(errData?.error || `Request failed (${response.status})`);
    }
    return await response.json();
}

function formatBytes(bytes) {
    if (!Number.isFinite(bytes) || bytes <= 0) {
        return '0 B';
    }

    const units = ['B', 'KB', 'MB', 'GB'];
    let value = bytes;
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
        value /= 1024;
        unitIndex += 1;
    }

    const precision = value >= 10 || unitIndex === 0 ? 0 : 1;
    return `${value.toFixed(precision)} ${units[unitIndex]}`;
}

function escapeHtml(value) {
    return String(value)
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
}

function updateUploadState(patch) {
    if (!state.upload) {
        return;
    }
    state.upload = { ...state.upload, ...patch };
    render();
}

function uploadFile(endpoint, formData, onProgress) {
    return new Promise((resolve, reject) => {
        const request = new XMLHttpRequest();
        request.open('POST', `${API_BASE}${endpoint}`);
        request.responseType = 'json';

        request.upload.addEventListener('progress', (event) => {
            onProgress({
                loaded: event.loaded,
                total: event.lengthComputable ? event.total : null
            });
        });

        request.addEventListener('load', () => {
            if (request.status >= 200 && request.status < 300) {
                resolve(request.response || { success: true });
                return;
            }

            const message = request.response?.error
                || request.response?.message
                || `Upload failed (${request.status})`;
            reject(new Error(message));
        });

        request.addEventListener('error', () => reject(new Error('Upload failed')));
        request.addEventListener('abort', () => reject(new Error('Upload aborted')));
        request.send(formData);
    });
}

function revokeDebugPreviewUrl() {
    if (!state.debugPreviewUrl) {
        return;
    }
    URL.revokeObjectURL(state.debugPreviewUrl);
    state.debugPreviewUrl = null;
}

async function callDebugPreviewEndpoint(action) {
    const response = await fetch(`${API_BASE}/debug/camera-preview/${action}`, {
        method: 'POST'
    });
    if (response.ok) {
        return;
    }

    let message = `Failed to ${action} camera preview`;
    const contentType = response.headers.get('content-type') || '';
    if (contentType.includes('application/json')) {
        const data = await response.json();
        message = data.error || message;
    } else {
        const text = await response.text();
        if (text) {
            message = text;
        }
    }
    throw new Error(message);
}

function stopDebugPreview({ clearImage = false, stopSession = false } = {}) {
    if (debugPreviewTimerId) {
        window.clearTimeout(debugPreviewTimerId);
        debugPreviewTimerId = 0;
    }
    if (debugPreviewAbortController) {
        debugPreviewAbortController.abort();
        debugPreviewAbortController = null;
    }
    if (clearImage) {
        revokeDebugPreviewUrl();
    }
    if (stopSession && state.debugPreviewSessionActive) {
        state.debugPreviewSessionActive = false;
        fetch(`${API_BASE}/debug/camera-preview/stop`, { method: 'POST' }).catch(() => {});
    }
}

function stopDebugLogs({ clearText = false } = {}) {
    if (debugLogsTimerId) {
        window.clearTimeout(debugLogsTimerId);
        debugLogsTimerId = 0;
    }
    if (debugLogsAbortController) {
        debugLogsAbortController.abort();
        debugLogsAbortController = null;
    }
    if (clearText) {
        state.debugLogsText = '';
    }
}

function stopDeviceStatusRefresh() {
    if (deviceStatusTimerId) {
        window.clearTimeout(deviceStatusTimerId);
        deviceStatusTimerId = 0;
    }
    if (deviceStatusAbortController) {
        deviceStatusAbortController.abort();
        deviceStatusAbortController = null;
    }
}

function scheduleDeviceStatusRefresh(delayMs = 15000) {
    if (state.loading || state.error) {
        return;
    }
    const effectiveDelayMs = state.view === 'debug-battery'
        ? Math.min(delayMs, 1000)
        : delayMs;
    if (deviceStatusTimerId) {
        window.clearTimeout(deviceStatusTimerId);
    }
    deviceStatusTimerId = window.setTimeout(() => {
        deviceStatusTimerId = 0;
        refreshDeviceStatus();
    }, effectiveDelayMs);
}

async function refreshDeviceStatus() {
    if (deviceStatusAbortController) {
        return;
    }

    const controller = new AbortController();
    deviceStatusAbortController = controller;
    try {
        const response = await fetch(`${API_BASE}/status?ts=${Date.now()}`, {
            cache: 'no-store',
            signal: controller.signal
        });
        if (!response.ok) {
            throw new Error(`Failed to load device status (${response.status})`);
        }

        state.deviceStatus = await response.json();
        state.deviceStatusError = '';
        scheduleDeviceStatusRefresh();
    } catch (err) {
        if (err.name === 'AbortError') {
            return;
        }
        state.deviceStatusError = err.message || 'Failed to load device status.';
        scheduleDeviceStatusRefresh(30000);
    } finally {
        if (deviceStatusAbortController === controller) {
            deviceStatusAbortController = null;
        }
        render();
    }
}

function scheduleDebugPreviewRefresh(delayMs = 1200) {
    if (state.view !== 'debug-camera') {
        return;
    }
    if (debugPreviewTimerId) {
        window.clearTimeout(debugPreviewTimerId);
    }
    debugPreviewTimerId = window.setTimeout(() => {
        debugPreviewTimerId = 0;
        refreshDebugPreview();
    }, delayMs);
}

function scheduleDebugLogsRefresh(delayMs = 1500) {
    if (state.view !== 'debug-logs') {
        return;
    }
    if (debugLogsTimerId) {
        window.clearTimeout(debugLogsTimerId);
    }
    debugLogsTimerId = window.setTimeout(() => {
        debugLogsTimerId = 0;
        refreshDebugLogs();
    }, delayMs);
}

function leaveDebugView() {
    stopDebugPreview({ clearImage: true, stopSession: true });
    stopDebugLogs({ clearText: true });
    state.debugPreviewLoading = false;
    state.debugPreviewError = '';
    state.debugPreviewUpdatedAt = 0;
    state.debugPreviewSessionActive = false;
    state.debugLogsLoading = false;
    state.debugLogsError = '';
    state.debugLogsUpdatedAt = 0;
    state.debugBatteryError = '';
}

function formatDebugTimestamp(timestamp) {
    if (!timestamp) {
        return '';
    }
    return new Date(timestamp).toLocaleTimeString([], {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

function formatBatteryVoltage(millivolts) {
    if (!Number.isFinite(millivolts) || millivolts <= 0) {
        return '';
    }
    return `${(millivolts / 1000).toFixed(2)} V`;
}

function batterySummaryText() {
    const battery = state.deviceStatus?.battery;
    if (!battery?.initialized) {
        return 'Battery telemetry not initialized.';
    }
    if (!battery?.reading_available) {
        return 'Battery reading unavailable.';
    }
    if (!battery?.has_reading) {
        return 'Waiting for first battery reading...';
    }
    if (!battery?.reading_stable || battery?.availability === 'settling') {
        const voltage = formatBatteryVoltage(battery.voltage_mv);
        return voltage ? `Battery settling · ${voltage}` : 'Battery settling...';
    }

    const parts = [
        `${battery.percent}%`,
        formatBatteryVoltage(battery.voltage_mv)
    ];
    return parts.join(' · ');
}

function batteryStateClass() {
    const battery = state.deviceStatus?.battery;
    if (battery?.is_critical) {
        return 'critical';
    }
    if (battery?.is_low) {
        return 'low';
    }
    if (!battery?.initialized || !battery?.reading_available || !battery?.has_reading) {
        return 'unavailable';
    }
    if (!battery?.reading_stable || battery?.availability === 'settling') {
        return 'settling';
    }
    return '';
}

function debugBatteryOverrideStatusText() {
    const override = state.deviceStatus?.battery?.debug_override;
    if (!override?.enabled) {
        return 'No fake battery override is armed.';
    }
    if (override.active) {
        return `Fake battery active at ${formatBatteryVoltage(override.target_mv)}.`;
    }
    const delaySeconds = Math.max(0, Math.ceil((override.activate_in_ms || 0) / 1000));
    return `Fake battery armed: ${formatBatteryVoltage(override.target_mv)} in ${delaySeconds}s.`;
}

async function setDebugBatteryOverride(voltageMv, delayMs = 10000) {
    state.debugBatteryError = '';
    render();
    try {
        await fetchAPI('/debug/battery-override', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                voltage_mv: voltageMv,
                delay_ms: delayMs
            })
        });
        await refreshDeviceStatus();
    } catch (err) {
        state.debugBatteryError = err.message || 'Failed to arm fake battery voltage.';
        render();
    }
}

async function clearDebugBatteryOverride() {
    state.debugBatteryError = '';
    render();
    try {
        await fetch(`${API_BASE}/debug/battery-override`, {
            method: 'DELETE'
        }).then(async response => {
            if (!response.ok) {
                const errData = await response.json().catch(() => null);
                throw new Error(errData?.error || `Request failed (${response.status})`);
            }
        });
        await refreshDeviceStatus();
    } catch (err) {
        state.debugBatteryError = err.message || 'Failed to clear fake battery voltage.';
        render();
    }
}

function applyDebugBatteryPreset(voltageMv) {
    const voltageInput = document.getElementById('debug-battery-voltage');
    const delayInput = document.getElementById('debug-battery-delay');
    if (voltageInput) voltageInput.value = (voltageMv / 1000).toFixed(2);
    if (delayInput) delayInput.value = '10';
    setDebugBatteryOverride(voltageMv, 10000);
}

function applyCustomDebugBatteryOverride(event) {
    event.preventDefault();
    const voltageInput = document.getElementById('debug-battery-voltage');
    const delayInput = document.getElementById('debug-battery-delay');
    const voltageValue = Number.parseFloat(voltageInput?.value || '');
    const delaySeconds = Number.parseFloat(delayInput?.value || '');
    if (!Number.isFinite(voltageValue) || !Number.isFinite(delaySeconds)) {
        state.debugBatteryError = 'Enter a valid voltage and delay.';
        render();
        return false;
    }

    const voltageMv = Math.round(voltageValue * 1000);
    const delayMs = Math.max(0, Math.round(delaySeconds * 1000));
    setDebugBatteryOverride(voltageMv, delayMs);
    return false;
}

function toggleDebugCameraKernel() {
    state.debugCameraKernel = !state.debugCameraKernel;
    state.debugPreviewError = '';
    if (state.view === 'debug-camera' && state.debugPreviewSessionActive) {
        stopDebugPreview();
        refreshDebugPreview();
        render();
        return;
    }
    render();
}

async function refreshDebugPreview() {
    if (state.view !== 'debug-camera' || debugPreviewAbortController) {
        return;
    }

    state.debugPreviewLoading = true;
    render();

    const controller = new AbortController();
    debugPreviewAbortController = controller;
    try {
        const kernelParam = state.debugCameraKernel ? '&kernel=1' : '';
        const response = await fetch(`${API_BASE}/debug/camera-frame?ts=${Date.now()}${kernelParam}`, {
            cache: 'no-store',
            signal: controller.signal
        });
        if (!response.ok) {
            let message = `Failed to load camera preview (${response.status})`;
            const contentType = response.headers.get('content-type') || '';
            if (contentType.includes('application/json')) {
                const data = await response.json();
                message = data.error || message;
            } else {
                const text = await response.text();
                if (text) {
                    message = text;
                }
            }
            throw new Error(message);
        }

        const previewBlob = await response.blob();
        const previewUrl = URL.createObjectURL(previewBlob);
        revokeDebugPreviewUrl();
        state.debugPreviewUrl = previewUrl;
        state.debugPreviewError = '';
        state.debugPreviewUpdatedAt = Date.now();
        scheduleDebugPreviewRefresh();
    } catch (err) {
        if (err.name === 'AbortError') {
            return;
        }
        state.debugPreviewError = err.message || 'Failed to load camera preview.';
        scheduleDebugPreviewRefresh(1800);
    } finally {
        state.debugPreviewLoading = false;
        if (debugPreviewAbortController === controller) {
            debugPreviewAbortController = null;
        }
        render();
    }
}

async function refreshDebugLogs() {
    if (state.view !== 'debug-logs' || debugLogsAbortController) {
        return;
    }

    state.debugLogsLoading = true;
    render();

    const controller = new AbortController();
    debugLogsAbortController = controller;
    try {
        const response = await fetch(`${API_BASE}/debug/logs?limit=160&ts=${Date.now()}`, {
            cache: 'no-store',
            signal: controller.signal
        });
        if (!response.ok) {
            throw new Error(`Failed to load logs (${response.status})`);
        }

        state.debugLogsText = await response.text();
        state.debugLogsError = '';
        state.debugLogsUpdatedAt = Date.now();
        scheduleDebugLogsRefresh();
    } catch (err) {
        if (err.name === 'AbortError') {
            return;
        }
        state.debugLogsError = err.message || 'Failed to load logs.';
        scheduleDebugLogsRefresh(2200);
    } finally {
        state.debugLogsLoading = false;
        if (debugLogsAbortController === controller) {
            debugLogsAbortController = null;
        }
        render();
    }
}

function openDebug(push = true) {
    if (push) {
        const currentHash = window.location.hash || '#/';
        if (!currentHash.startsWith('#/debug') && !currentHash.startsWith('#/software')) {
            debugReturnHash = currentHash;
        }
        window.location.hash = '/debug';
        return;
    }

    leaveDebugView();
    state.view = 'debug-menu';
    state.loading = false;
    render();
}

function openSoftware(push = true) {
    if (push) {
        const currentHash = window.location.hash || '#/';
        if (!currentHash.startsWith('#/debug') && !currentHash.startsWith('#/software')) {
            debugReturnHash = currentHash;
        }
        window.location.hash = '/software';
        return;
    }

    leaveDebugView();
    state.view = 'software';
    state.loading = false;
    render();
}

function closeDropdowns() {
    document.querySelectorAll('details.dropdown[open]').forEach(el => {
        el.removeAttribute('open');
    });
}

async function openDebugCamera(push = true) {
    if (push) {
        const currentHash = window.location.hash || '#/';
        if (!currentHash.startsWith('#/debug') && !currentHash.startsWith('#/software')) {
            debugReturnHash = currentHash;
        }
        window.location.hash = '/debug/camera';
        return;
    }

    stopDebugPreview();
    state.loading = true;
    render();
    try {
        await callDebugPreviewEndpoint('start');
    } catch (err) {
        state.loading = false;
        state.view = 'debug-menu';
        state.debugPreviewError = err.message || 'Failed to start camera preview.';
        render();
        return;
    }

    state.view = 'debug-camera';
    state.loading = false;
    state.debugPreviewError = '';
    state.debugPreviewSessionActive = true;
    render();
    refreshDebugPreview();
}

function openDebugLogs(push = true) {
    if (push) {
        const currentHash = window.location.hash || '#/';
        if (!currentHash.startsWith('#/debug') && !currentHash.startsWith('#/software')) {
            debugReturnHash = currentHash;
        }
        window.location.hash = '/debug/logs';
        return;
    }

    stopDebugPreview({ clearImage: true, stopSession: true });
    stopDebugLogs();
    state.view = 'debug-logs';
    state.loading = false;
    state.debugLogsError = '';
    render();
    refreshDebugLogs();
}

function openDebugBattery(push = true) {
    if (push) {
        const currentHash = window.location.hash || '#/';
        if (!currentHash.startsWith('#/debug') && !currentHash.startsWith('#/software')) {
            debugReturnHash = currentHash;
        }
        window.location.hash = '/debug/battery';
        return;
    }

    stopDebugPreview({ clearImage: true, stopSession: true });
    stopDebugLogs();
    state.view = 'debug-battery';
    state.loading = false;
    state.debugBatteryError = '';
    render();
    refreshDeviceStatus();
}

function leaveDebugMenu() {
    if (state.view === 'debug-camera' || state.view === 'debug-logs' || state.view === 'debug-battery') {
        window.location.hash = '/debug';
        return;
    }
    const targetHash = debugReturnHash && debugReturnHash !== '#/debug' && debugReturnHash !== '#/software'
        ? debugReturnHash
        : '#/';
    window.location.hash = targetHash.startsWith('#')
        ? targetHash.substring(1)
        : targetHash;
}

async function navigate() {
    const hash = window.location.hash;
    if (hash === '#/debug/camera') {
        await openDebugCamera(false);
    } else if (hash === '#/debug/logs') {
        openDebugLogs(false);
    } else if (hash === '#/debug/battery') {
        openDebugBattery(false);
    } else if (hash === '#/debug') {
        openDebug(false);
    } else if (hash === '#/software') {
        openSoftware(false);
    } else if (hash.startsWith('#/dir/')) {
        const path = decodeURIComponent(hash.substring(6));
        await enterDirectory(path, false);
    } else {
        await loadDashboard(false);
    }
}

async function loadDashboard(push = true) {
    if (push) window.location.hash = '/';
    leaveDebugView();
    state.loading = true;
    state.error = null;
    render();
    try {
        state.directories = await fetchAPI('/list');
        state.view = 'dashboard';
    } catch (err) {
        state.error = err.message || 'Failed to connect to Zauberbox';
    } finally {
        state.loading = false;
        render();
        if (!state.error) {
            refreshDeviceStatus();
        }
    }
}

async function enterDirectory(path, push = true) {
    if (push) window.location.hash = `/dir/${encodeURIComponent(path)}`;
    leaveDebugView();
    state.loading = true;
    state.error = null;
    state.currentPath = path;
    render();
    try {
        state.files = await fetchAPI(`/files?path=${encodeURIComponent(path)}`);
        if (state.directories.length === 0) {
            state.directories = await fetchAPI('/list');
        }
        state.view = 'directory';
    } catch (err) {
        state.error = err.message || 'Failed to load directory';
    } finally {
        state.loading = false;
        render();
        if (!state.error) {
            refreshDeviceStatus();
        }
    }
}

async function handleMkdir() {
    let nextNum = (state.directories.length + 1).toString().padStart(3, '0');
    const nums = state.directories
        .map(d => parseInt(d.name))
        .filter(n => !isNaN(n));
    if (nums.length > 0) {
        nextNum = (Math.max(...nums) + 1).toString().padStart(3, '0');
    }

    const name = await showModal({
        title: 'New Folder',
        message: 'Enter folder name:',
        input: true,
        value: nextNum,
        confirmText: 'Create'
    });
    if (!name) return;

    await fetchAPI('/mkdir', { 
        method: 'POST', 
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ name: name }) 
    });
    await loadDashboard(false);
}

async function handleRmdir(e, name) {
    e.stopPropagation();
    const confirmed = await showModal({
        title: 'Delete Directory',
        message: `Delete directory "${name}" and all its contents permanently?`,
        confirmText: 'Delete'
    });
    if (!confirmed) return;
    await fetchAPI('/rmdir', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ name: name })
    });
    await loadDashboard(false);
}

async function handleRename(oldName) {
    const newName = await showModal({
        title: 'Rename File',
        message: `Rename "${oldName}" to:`,
        input: true,
        value: oldName,
        confirmText: 'Rename'
    });
    if (!newName || newName === oldName) return;
    await fetchAPI('/rename', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ path: state.currentPath, old_name: oldName, new_name: newName })
    });
    enterDirectory(state.currentPath);
}

async function handleDelete(fileName) {
    const confirmed = await showModal({
        title: 'Delete File',
        message: `Delete "${fileName}" permanently?`,
        confirmText: 'Delete'
    });
    if (!confirmed) return;
    await fetchAPI('/delete', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ path: state.currentPath, file_name: fileName })
    });
    enterDirectory(state.currentPath);
}

async function handleUpload(files, forcedType = null) {
    if (!files || files.length === 0 || state.upload || !state.currentPath) return;

    const type = forcedType || 'file';
    const targetPath = state.currentPath;
    const preparedFiles = [];
    for (let file of Array.from(files)) {
        const lastModifiedMs = typeof file.lastModified === 'number' && file.lastModified > 0
            ? file.lastModified
            : Date.now();
        const uploadFileCandidate = type === 'cover'
            ? await convertToJpeg(file)
            : file;
        preparedFiles.push({
            file: uploadFileCandidate,
            lastModifiedMs
        });
    }

    const totalBytes = preparedFiles.reduce((sum, entry) => sum + Math.max(entry.file.size || 0, 1), 0);
    state.upload = {
        currentFileName: preparedFiles[0]?.file.name || '',
        currentFileLoaded: 0,
        currentFileSize: preparedFiles[0]?.file.size || 0,
        completedBytes: 0,
        completedFiles: 0,
        totalBytes,
        totalFiles: preparedFiles.length,
        type
    };
    render();

    try {
        for (const { file, lastModifiedMs } of preparedFiles) {
            updateUploadState({
                currentFileName: file.name,
                currentFileLoaded: 0,
                currentFileSize: file.size || 0
            });

            const formData = new FormData();
            formData.append('file', file);
            await uploadFile(
                `/upload?path=${encodeURIComponent(targetPath)}&type=${encodeURIComponent(type)}&last_modified_ms=${encodeURIComponent(String(lastModifiedMs))}`,
                formData,
                ({ loaded, total }) => {
                    updateUploadState({
                        currentFileLoaded: loaded,
                        currentFileSize: total ?? file.size ?? 0
                    });
                }
            );

            updateUploadState({
                completedBytes: state.upload.completedBytes + Math.max(file.size || 0, 1),
                completedFiles: state.upload.completedFiles + 1,
                currentFileLoaded: file.size || 0,
                currentFileSize: file.size || 0
            });
        }

        state.directories = await fetchAPI('/list');
        if (state.view === 'directory' && state.currentPath === targetPath) {
            await enterDirectory(targetPath, false);
        }
    } catch (err) {
        await showModal({
            title: 'Upload Failed',
            message: err.message || 'Failed to upload file.',
            confirmText: 'OK',
            showCancel: false
        });
    } finally {
        state.upload = null;
        render();
    }
}

function mmToPx(mm) {
    return Math.round((mm / MM_TO_INCH) * PRINT_DPI);
}

async function loadCoverImage(dirName, coverUrl) {
    const img = new Image();
    img.crossOrigin = "anonymous";
    await new Promise((resolve, reject) => {
        img.onload = resolve;
        img.onerror = () => reject(new Error(`Failed to load cover: ${dirName}`));
        img.src = coverUrl;
    });
    return img;
}

function drawSquareCover(ctx, img, xOffset, yOffset, size) {
    const imgAspect = img.width / img.height;
    let sx;
    let sy;
    let sWidth;
    let sHeight;

    if (imgAspect > 1) {
        sHeight = img.height;
        sWidth = img.height;
        sx = (img.width - sWidth) / 2;
        sy = 0;
    } else {
        sWidth = img.width;
        sHeight = img.width;
        sx = 0;
        sy = (img.height - sHeight) / 2;
    }

    ctx.drawImage(img, sx, sy, sWidth, sHeight, xOffset, yOffset, size, size);
}

function drawTextWithOutline(ctx, text, x, y) {
    ctx.strokeStyle = 'rgba(0, 0, 0, 0.75)';
    ctx.lineWidth = 5;
    ctx.lineJoin = 'round';
    ctx.strokeText(text, x, y);
    ctx.fillStyle = '#ffffff';
    ctx.fillText(text, x, y);
}

function fitTextToWidth(ctx, text, baseSize, maxWidth, fontWeight = '') {
    const minSize = Math.max(14, Math.round(baseSize * 0.65));
    let fontSize = baseSize;

    while (fontSize > minSize) {
        ctx.font = `${fontWeight}${fontSize}px sans-serif`;
        if (ctx.measureText(text).width <= maxWidth) {
            return { text, fontSize };
        }
        fontSize -= 1;
    }

    ctx.font = `${fontWeight}${minSize}px sans-serif`;
    if (ctx.measureText(text).width <= maxWidth) {
        return { text, fontSize: minSize };
    }

    let truncated = text;
    while (truncated.length > 1 && ctx.measureText(`${truncated}...`).width > maxWidth) {
        truncated = truncated.slice(0, -1);
    }

    return {
        text: truncated.length < text.length ? `${truncated}...` : truncated,
        fontSize: minSize
    };
}

function drawCoverText(ctx, mp3s, xOffset, yOffset, size) {
    if (mp3s.length === 0) {
        return;
    }

    const padding = Math.max(18, Math.round(size * 0.05));
    const singleTrackSize = Math.max(22, Math.round(size * 0.062));
    const multiTrackSize = Math.max(18, Math.round(size * 0.05));
    const lineGap = Math.max(6, Math.round(size * 0.012));
    const baseTrackSize = mp3s.length === 1 ? singleTrackSize : multiTrackSize;
    const maxTrackLines = Math.max(1, Math.floor((size - padding * 2) / (baseTrackSize + lineGap)));
    const visibleTracks = mp3s.slice(0, maxTrackLines);
    const overlayHeight = padding * 2 + visibleTracks.length * (baseTrackSize + lineGap);
    const maxTextWidth = size - padding * 2 - 8;

    const gradient = ctx.createLinearGradient(0, yOffset + size - overlayHeight, 0, yOffset + size);
    gradient.addColorStop(0, 'rgba(0, 0, 0, 0)');
    gradient.addColorStop(1, 'rgba(0, 0, 0, 0.78)');
    ctx.fillStyle = gradient;
    ctx.fillRect(xOffset, yOffset + size - overlayHeight, size, overlayHeight);

    ctx.textBaseline = 'alphabetic';
    let currentY = yOffset + size - overlayHeight + padding + baseTrackSize;
    visibleTracks.forEach((track) => {
        const fitted = fitTextToWidth(ctx, track, baseTrackSize, maxTextWidth, mp3s.length === 1 ? 'bold ' : '');
        ctx.font = `${mp3s.length === 1 ? 'bold ' : ''}${fitted.fontSize}px sans-serif`;
        drawTextWithOutline(ctx, fitted.text, xOffset + padding, currentY);
        currentY += baseTrackSize + lineGap;
    });
}

async function generateSingleCard(ctx, dirName, coverUrl, mp3s, xOffset, yOffset, width, height) {
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(xOffset, yOffset, width, height);

    const img = await loadCoverImage(dirName, coverUrl);
    const squareSize = Math.min(height, width / 2);
    const contentX = xOffset + (width - squareSize * 2) / 2;
    const squareY = yOffset + (height - squareSize) / 2;
    const previewX = contentX;
    const qrSquareX = contentX + squareSize;

    drawSquareCover(ctx, img, previewX, squareY, squareSize);
    drawCoverText(ctx, mp3s, previewX, squareY, squareSize);

    const qrQuietZone = Math.max(12, Math.round(squareSize * 0.08));
    const qrSize = squareSize - (qrQuietZone * 2);
    const uri = `file://${dirName}`;
    const qrContainer = document.createElement('div');
    new QRCode(qrContainer, {
        text: uri,
        width: qrSize,
        height: qrSize,
        colorDark: "#000000",
        colorLight: "#ffffff",
        correctLevel: QRCode.CorrectLevel.H
    });

    await new Promise(resolve => setTimeout(resolve, 50));
    const qrCanvas = qrContainer.querySelector('canvas');
    const qrX = qrSquareX + (squareSize - qrSize) / 2;
    const qrY = squareY + (squareSize - qrSize) / 2;
    ctx.drawImage(qrCanvas, qrX, qrY);
}

async function handleGenerateCard() {
    const coverUrl = state.files.find(f => f.name.toLowerCase() === 'cover.jpg')
        ? `${API_BASE}/file?path=${encodeURIComponent(state.currentPath)}&name=${encodeURIComponent('cover.jpg')}`
        : null;

    if (!coverUrl) {
        await showModal({ title: 'Error', message: 'Please upload a cover image first.', confirmText: 'OK', showCancel: false });
        return;
    }

    state.loading = true; render();

    try {
        const WIDTH_PX = mmToPx(TILE_WIDTH_MM);
        const HEIGHT_PX = mmToPx(TILE_HEIGHT_MM);

        const canvas = document.createElement('canvas');
        canvas.width = WIDTH_PX;
        canvas.height = HEIGHT_PX;
        const ctx = canvas.getContext('2d');

        const mp3s = state.files
            .filter(f => f.name.toLowerCase().endsWith('.mp3'))
            .map(f => f.name.replace(/\.mp3$/i, ''))
            .sort();

        await generateSingleCard(ctx, state.currentPath, coverUrl, mp3s, 0, 0, WIDTH_PX, HEIGHT_PX);

        const dataUrl = canvas.toDataURL('image/jpeg', 0.95);
        const link = document.createElement('a');
        link.download = `tile_${state.currentPath}.jpg`;
        link.href = dataUrl;
        link.click();

    } catch (err) {
        await showModal({ title: 'Error', message: 'Failed to generate tile.', confirmText: 'OK', showCancel: false });
    } finally {
        state.loading = false; render();
    }
}

function drawSheetGuides(ctx, width, height, rowHeight, rowCount) {
    const guideWidthPx = Math.max(1, mmToPx(0.2));
    const inset = guideWidthPx / 2;
    const maxX = Math.max(inset, width - inset);
    const maxY = Math.max(inset, height - inset);

    ctx.save();
    ctx.strokeStyle = '#000000';
    ctx.lineWidth = guideWidthPx;

    for (let row = 1; row < rowCount; row++) {
        const y = Math.min(maxY, Math.max(inset, row * rowHeight));
        ctx.beginPath();
        ctx.moveTo(inset, y);
        ctx.lineTo(maxX, y);
        ctx.stroke();
    }

    ctx.strokeRect(inset, inset, Math.max(0, width - guideWidthPx), Math.max(0, height - guideWidthPx));
    ctx.restore();
}

async function handleDownloadSheets() {
    if (state.selectedDirs.size === 0) return;
    state.loading = true; render();
    try {
        const TILE_W_PX = mmToPx(TILE_WIDTH_MM);
        const TILE_H_PX = mmToPx(TILE_HEIGHT_MM);
        const WIDTH_PX = TILE_W_PX;
        const HEIGHT_PX = TILE_H_PX * SHEET_ROWS;

        const selectedNames = Array.from(state.selectedDirs);
        for (let i = 0; i < selectedNames.length; i += SHEET_ROWS) {
            const chunk = selectedNames.slice(i, i + SHEET_ROWS);
            const canvas = document.createElement('canvas');
            canvas.width = WIDTH_PX; canvas.height = HEIGHT_PX;
            const ctx = canvas.getContext('2d');
            ctx.fillStyle = 'white'; ctx.fillRect(0, 0, WIDTH_PX, HEIGHT_PX);

            for (let j = 0; j < chunk.length; j++) {
                const name = chunk[j];
                const files = await fetchAPI(`/files?path=${encodeURIComponent(name)}`);
                const coverUrl = `${API_BASE}/file?path=${encodeURIComponent(name)}&name=${encodeURIComponent('cover.jpg')}`;
                const hasCover = files.some(f => f.name.toLowerCase() === 'cover.jpg');
                const mp3s = files
                    .filter(f => f.name.toLowerCase().endsWith('.mp3'))
                    .map(f => f.name.replace(/\.mp3$/i, ''))
                    .sort();
                if (!hasCover) {
                    throw new Error(`Missing cover image for ${name}`);
                }
                await generateSingleCard(ctx, name, coverUrl, mp3s, 0, j * TILE_H_PX, TILE_W_PX, TILE_H_PX);
            }

            drawSheetGuides(ctx, WIDTH_PX, HEIGHT_PX, TILE_H_PX, SHEET_ROWS);

            const link = document.createElement('a');
            link.download = `sheet_${Math.floor(i / SHEET_ROWS) + 1}.jpg`;
            link.href = canvas.toDataURL('image/jpeg', 0.95);
            link.click();
            await new Promise(r => setTimeout(r, 500));
        }
        toggleSelectionMode();
    } catch (err) {
        console.error(err);
        await showModal({ title: 'Error', message: 'Failed to generate sheets.', confirmText: 'OK', showCancel: false });
    } finally {
        state.loading = false; render();
    }
}

async function convertToJpeg(file) {
    return new Promise((resolve) => {
        const reader = new FileReader();
        reader.onload = (e) => {
            const img = new Image();
            img.onload = () => {
                const canvas = document.createElement('canvas');
                const scale = Math.min(1, COVER_MAX_DIMENSION / Math.max(img.width, img.height));
                canvas.width = Math.max(1, Math.round(img.width * scale));
                canvas.height = Math.max(1, Math.round(img.height * scale));
                const ctx = canvas.getContext('2d');
                ctx.fillStyle = '#ffffff';
                ctx.fillRect(0, 0, canvas.width, canvas.height);
                ctx.drawImage(img, 0, 0, canvas.width, canvas.height);

                const tryQualityAt = (index) => {
                    const quality = COVER_JPEG_QUALITIES[Math.min(index, COVER_JPEG_QUALITIES.length - 1)];
                    canvas.toBlob((blob) => {
                        if (!blob) {
                            resolve(new File([], "cover.jpg", { type: 'image/jpeg' }));
                            return;
                        }
                        if (blob.size <= COVER_TARGET_MAX_BYTES || index >= COVER_JPEG_QUALITIES.length - 1) {
                            resolve(new File([blob], "cover.jpg", { type: 'image/jpeg' }));
                            return;
                        }
                        tryQualityAt(index + 1);
                    }, 'image/jpeg', quality);
                };

                tryQualityAt(0);
            };
            img.src = e.target.result;
        };
        reader.readAsDataURL(file);
    });
}

function setupDragAndDrop() {
    const zone = document.getElementById('drop-zone');
    if (!zone || state.upload) return;

    ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(name => {
        zone.addEventListener(name, e => { e.preventDefault(); e.stopPropagation(); });
    });

    zone.addEventListener('dragover', () => zone.classList.add('drag-over'));
    ['dragleave', 'drop'].forEach(name => {
        zone.addEventListener(name, () => zone.classList.remove('drag-over'));
    });

    zone.addEventListener('drop', e => {
        zone.classList.remove('drag-over');
        handleUpload(e.dataTransfer.files, 'file');
    });
}

function render() {
    const app = document.getElementById('app');
    const navLeft = document.getElementById('nav-left');
    const navRight = document.getElementById('nav-right');
    
    // Capture scroll position of logs if they exist
    let logsScroll = null;
    const oldLogShell = document.querySelector('.debug-log-shell');
    if (oldLogShell) {
        logsScroll = {
            top: oldLogShell.scrollTop,
            height: oldLogShell.scrollHeight,
            client: oldLogShell.clientHeight,
            atBottom: (oldLogShell.scrollHeight - oldLogShell.scrollTop) <= (oldLogShell.clientHeight + 20)
        };
    }

    if (state.loading) {
        app.innerHTML = '<div aria-busy="true" id="loading">Working...</div>';
        return;
    }

    if (state.error) {
        app.innerHTML = `
            <div style="text-align: center; margin-top: 4rem;">
                <h2 style="color: var(--pico-error-background);">Connection Error</h2>
                <p>${escapeHtml(state.error)}</p>
                <button class="primary" style="margin-top: 1rem;" onclick="navigate()">Retry</button>
            </div>
        `;
        return;
    }

    if (state.view === 'dashboard') {
        const battery = state.deviceStatus?.battery;
        const batterySummary = batterySummaryText();
        const batteryState = batteryStateClass();
		const batteryDetail = state.deviceStatusError ? `title="${escapeHtml(state.deviceStatusError)}"` : '';
        const batteryIcon = ICONS[batteryIconName()];

        navLeft.innerHTML = `<li><strong class="rainbow">Zauberbox</strong></li>`;
        navRight.innerHTML = `
            <li class="nav-battery ${batteryState}" ${batteryDetail}>
                <span class="nav-battery-icon" aria-hidden="true">${batteryIcon}</span>
                <span class="nav-battery-text">${escapeHtml(batterySummary)}</span>
            </li>
            <li>
                <details class="dropdown nav-dropdown" dir="rtl">
                    <summary class="contrast outline" style="padding: 4px 8px; list-style: none;">${ICONS.menu}</summary>
                    <ul dir="ltr">
                        <li><a href="#/debug" onclick="closeDropdowns()">Debug Menu</a></li>
                        <li><a href="#/software" onclick="closeDropdowns()">Software</a></li>
                    </ul>
                </details>
            </li>
        `;

        let html = `<div class="${state.selectionMode ? 'selection-mode' : ''}">`;
        html += `
            <div class="header-row">
                <h2>Directories</h2>
                <button class="${state.selectionMode ? 'primary' : 'contrast outline'} select-btn" onclick="toggleSelectionMode()">${state.selectionMode ? 'Cancel' : 'Select Folders'}</button>
            </div>
            <div class="dir-grid">
        `;
        state.directories.forEach(dir => {
            const isSelected = state.selectedDirs.has(dir.name);
            html += `
                <div class="dir-card ${isSelected ? 'selected' : ''}" onclick="${state.selectionMode ? `toggleDirSelection('${dir.name}')` : `enterDirectory('${dir.name}')`}">
                    ${state.selectionMode ? `<div class="select-overlay">${ICONS.check}</div>` : `<div class="delete-btn" onclick="handleRmdir(event, '${dir.name}')" title="Delete Directory">${ICONS.trash}</div>`}
                    ${dir.cover ? `<img src="${dir.cover}">` : `<div class="placeholder-img"><span>No Cover</span></div>`}
                    <span class="name">${dir.name}</span>
                    <span class="first-mp3">${dir.first_mp3 || 'Empty'}</span>
                </div>
            `;
        });
        if (!state.selectionMode) {
            html += `
                <div class="dir-card add-card" onclick="handleMkdir()">
                    ${ICONS.addFolder}
                    <span class="name">New Folder</span>
                </div>
            `;
        }
        html += `</div></div>`;

        if (state.selectionMode && state.selectedDirs.size > 0) {
            html += `
                <div class="selection-bar">
                    <span>${state.selectedDirs.size} selected</span>
                    <button class="primary" onclick="handleDownloadSheets()">${ICONS.download} Download Sheets</button>
                </div>
            `;
        }
        app.innerHTML = html;
    } else if (state.view === 'software') {
        navLeft.innerHTML = `
            <li><button class="contrast outline" style="padding: 4px 8px; border:none;" onclick="leaveDebugMenu()">${ICONS.back}</button></li>
            <li><strong>Software</strong></li>
        `;
        navRight.innerHTML = ``;

        app.innerHTML = `
            <section>
                <article>
                    <header><strong>Used Open Source Software</strong></header>
                    <p>Zauberbox is built using these amazing open source projects:</p>
                    <ul>
                        <li><a href="https://picocss.com" target="_blank"><strong>Pico CSS</strong></a> (MIT) - Minimal design system for the web interface.</li>
                        <li><a href="https://lucide.dev" target="_blank"><strong>Lucide Icons</strong></a> (ISC) - Beautiful & consistent icons.</li>
                        <li><a href="https://github.com/schreibfaul1/ESP32-audioI2S/" target="_blank"><strong>ESP32-audioI2S</strong></a> (GPL v3.0) - Audio player library.</li>
                        <li><a href="https://github.com/davidshimjs/qrcodejs" target="_blank"><strong>qrcode.js</strong></a> (MIT) - QR code generation in the browser.</li>
                        <li><a href="https://github.com/ayresnet/AyresWiFiManager" target="_blank"><strong>AyresWiFiManager</strong></a> (MIT) - Captive portal and WiFi management.</li>
                        <li><a href="https://github.com/dlbeer/quirc" target="_blank"><strong>quirc</strong></a> (ISC) - QR code decoder library used in firmware.</li>
                        <li><a href="https://github.com/bblanchon/ArduinoJson" target="_blank"><strong>ArduinoJson</strong></a> (MIT) - JSON library.</li>
                        <li><a href="https://github.com/adafruit/adafruit_neopixel" target="_blank"><strong>Adafruit_NeoPixel</strong></a> (LPGL v3.0) - controlling single-wire LED pixels.</li>
                        <li><a href="https://github.com/RobTillaart/TCA9555" target="_blank"><strong>TCA9555</strong></a> (MIT) - library for I2C TCA9555 16 channel port expander.</li>
                    </ul>
                </article>
            </section>
        `;
    } else if (state.view === 'debug-menu') {
        navLeft.innerHTML = `
            <li><button class="contrast outline" style="padding: 4px 8px; border:none;" onclick="leaveDebugMenu()">${ICONS.back}</button></li>
            <li><strong>Debug</strong></li>
        `;
        navRight.innerHTML = ``;

        app.innerHTML = `
            <section class="debug-grid debug-menu-grid">
                ${state.debugPreviewError ? `<article class="debug-card"><p class="debug-status error" style="margin:0;">${state.debugPreviewError}</p></article>` : ''}
                <article class="debug-menu-card" onclick="openDebugCamera()">
                    <strong>Camera Preview</strong>
                    <p>Inspect the current camera frame captured and converted on the device.</p>
                </article>
                <article class="debug-menu-card" onclick="openDebugLogs()">
                    <strong>Logs</strong>
                    <p>View recent firmware log output captured on the device.</p>
                </article>
                <article class="debug-menu-card" onclick="openDebugBattery()">
                    <strong>Battery Test</strong>
                    <p>Arm a delayed fake battery voltage in RAM to test sleep and warning behavior.</p>
                </article>
            </section>
        `;
    } else if (state.view === 'debug-camera') {
        navLeft.innerHTML = `
            <li><button class="contrast outline" style="padding: 4px 8px; border:none;" onclick="leaveDebugMenu()">${ICONS.back}</button></li>
            <li><strong>Camera Preview</strong></li>
        `;
        navRight.innerHTML = ``;

        const statusText = state.debugPreviewError
            ? state.debugPreviewError
            : state.debugPreviewUpdatedAt
                ? `Last frame ${formatDebugTimestamp(state.debugPreviewUpdatedAt)}`
                : (state.debugPreviewLoading ? 'Loading first frame...' : 'Waiting for first frame...');

        app.innerHTML = `
            <section class="debug-grid">
                <article class="debug-card">
                    <header class="debug-card-header">
                        <div>
                            <strong>Camera Preview</strong>
                            <p>Single-frame snapshots converted on the device.</p>
                        </div>
                        <label style="margin: 0; display: flex; align-items: center; gap: 0.5rem; font-size: 0.9rem; cursor: pointer;">
                            <input type="checkbox" ${state.debugCameraKernel ? 'checked' : ''} onchange="toggleDebugCameraKernel()" style="margin: 0;">
                            Kernel Cross 7
                        </label>
                    </header>
                    <div class="debug-frame-shell">
                        ${state.debugPreviewUrl
                            ? `<img class="debug-frame-image" src="${state.debugPreviewUrl}" alt="Camera preview frame">`
                            : `<div class="debug-frame-placeholder">${state.debugPreviewLoading ? 'Loading frame...' : 'No frame available yet'}</div>`}
                    </div>
                    <p class="debug-status${state.debugPreviewError ? ' error' : ''}">${statusText}</p>
                    <small>Preview pauses when QR scanning or audio playback is active.</small>
                </article>
            </section>
        `;
    } else if (state.view === 'debug-logs') {
        navLeft.innerHTML = `
            <li><button class="contrast outline" style="padding: 4px 8px; border:none;" onclick="leaveDebugMenu()">${ICONS.back}</button></li>
            <li><strong>Logs</strong></li>
        `;
        navRight.innerHTML = ``;

        const statusText = state.debugLogsError
            ? state.debugLogsError
            : state.debugLogsUpdatedAt
                ? `Last update ${formatDebugTimestamp(state.debugLogsUpdatedAt)}`
                : (state.debugLogsLoading ? 'Loading logs...' : 'Waiting for first log fetch...');

        // Surgical update: If we are already in the logs view, just update the text
        const existingOutput = document.querySelector('.debug-log-output');
        const existingStatus = document.querySelector('.debug-status');
        if (existingOutput && existingStatus) {
            const shell = existingOutput.parentElement;
            const atBottom = (shell.scrollHeight - shell.scrollTop) <= (shell.clientHeight + 20);
            
            // Avoid resetting selection if user is currently selecting text
            const selection = window.getSelection();
            const hasSelection = selection && selection.toString().length > 0;
            const isSelectionInOutput = hasSelection && existingOutput.contains(selection.anchorNode);

            if (!isSelectionInOutput) {
                const newText = state.debugLogsText || '';
                if (existingOutput.textContent !== newText) {
                    existingOutput.textContent = newText;
                    if (atBottom) {
                        shell.scrollTop = shell.scrollHeight;
                    }
                }
            }
            
            existingStatus.textContent = statusText;
            existingStatus.className = `debug-status${state.debugLogsError ? ' error' : ''}`;
        } else {
            app.innerHTML = `
                <section class="debug-grid">
                    <article class="debug-card">
                        <header class="debug-card-header">
                            <div>
                                <strong>Logs</strong>
                                <p>Recent firmware log lines stored in the device memory buffer.</p>
                            </div>
                        </header>
                        <div class="debug-log-shell">
                            <pre class="debug-log-output">${escapeHtml(state.debugLogsText || '')}</pre>
                        </div>
                        <p class="debug-status${state.debugLogsError ? ' error' : ''}">${statusText}</p>
                        <small>Logs refresh automatically. The buffer keeps only the most recent entries.</small>
                    </article>
                </section>
            `;
            // Scroll to bottom on initial load
            const shell = app.querySelector('.debug-log-shell');
            if (shell) shell.scrollTop = shell.scrollHeight;
        }
    } else if (state.view === 'debug-battery') {
        navLeft.innerHTML = `
            <li><button class="contrast outline" style="padding: 4px 8px; border:none;" onclick="leaveDebugMenu()">${ICONS.back}</button></li>
            <li><strong>Battery Test</strong></li>
        `;
        navRight.innerHTML = ``;

        const battery = state.deviceStatus?.battery;
        const override = battery?.debug_override;
        const batteryText = batterySummaryText();
        const overrideText = debugBatteryOverrideStatusText();
        const existingLiveStatus = document.querySelector('.debug-battery-live-status');
        const existingOverrideStatus = document.querySelector('.debug-battery-override-status');
        const existingError = document.querySelector('.debug-battery-error');
        const existingNote = document.querySelector('.debug-battery-note');
        const existingVoltageInput = document.getElementById('debug-battery-voltage');
        const existingDelayInput = document.getElementById('debug-battery-delay');
        if (existingLiveStatus &&
            existingOverrideStatus &&
            existingError &&
            existingNote &&
            existingVoltageInput &&
            existingDelayInput) {
            existingLiveStatus.textContent = batteryText;
            existingOverrideStatus.textContent = overrideText;
            existingError.textContent = state.debugBatteryError || '';
            existingError.className = `debug-status debug-battery-error${state.debugBatteryError ? ' error' : ''}`;
            existingNote.textContent = override?.active
                ? 'Effective battery is currently overridden. Sleep, LED, and warning logic use the fake voltage.'
                : 'Use a delay so you can start playback or enter the state you want to test first.';

            if (document.activeElement !== existingVoltageInput && override?.enabled) {
                existingVoltageInput.value = ((override.target_mv || 0) / 1000).toFixed(2);
            }
            if (document.activeElement !== existingDelayInput && override?.enabled && !override.active) {
                existingDelayInput.value = String(Math.max(0, Math.ceil((override.activate_in_ms || 0) / 1000)));
            }
        } else {
            app.innerHTML = `
                <section class="debug-grid">
                    <article class="debug-card">
                        <header class="debug-card-header">
                            <div>
                                <strong>Battery Test</strong>
                                <p>Schedules a RAM-only fake battery voltage. It is applied after a delay and disappears on reboot.</p>
                            </div>
                        </header>
                        <p class="debug-status debug-battery-error${state.debugBatteryError ? ' error' : ''}">${escapeHtml(state.debugBatteryError || '')}</p>
                        <p class="debug-status debug-battery-live-status">${escapeHtml(batteryText)}</p>
                        <p class="debug-status debug-battery-override-status">${escapeHtml(overrideText)}</p>
                        <small class="debug-battery-note">${override?.active ? 'Effective battery is currently overridden. Sleep, LED, and warning logic use the fake voltage.' : 'Use a delay so you can start playback or enter the state you want to test first.'}</small>
                        <div class="debug-battery-actions">
                            <button class="secondary" type="button" onclick="applyDebugBatteryPreset(3600)">3.60V in 10s</button>
                            <button class="secondary" type="button" onclick="applyDebugBatteryPreset(3450)">3.45V in 10s</button>
                            <button class="contrast outline" type="button" onclick="clearDebugBatteryOverride()">Clear Override</button>
                        </div>
                        <form class="debug-battery-form" onsubmit="return applyCustomDebugBatteryOverride(event)">
                            <label>
                                Voltage (V)
                                <input id="debug-battery-voltage" type="number" min="3.0" max="4.5" step="0.01" value="${override?.enabled ? ((override.target_mv || 0) / 1000).toFixed(2) : '3.45'}">
                            </label>
                            <label>
                                Delay (s)
                                <input id="debug-battery-delay" type="number" min="0" max="600" step="1" value="${override?.enabled && !override.active ? Math.max(0, Math.ceil((override.activate_in_ms || 0) / 1000)) : 10}">
                            </label>
                            <button type="submit">Arm Custom Voltage</button>
                        </form>
                    </article>
                </section>
            `;
        }
    } else {
        const uploadState = state.upload;
        const uploadInProgress = Boolean(uploadState);
        const totalBytes = uploadState ? Math.max(uploadState.totalBytes, 1) : 1;
        const uploadedBytes = uploadState
            ? Math.min(
                uploadState.completedBytes + uploadState.currentFileLoaded,
                uploadState.totalBytes
            )
            : 0;
        const uploadPercent = uploadState
            ? Math.round((uploadedBytes / totalBytes) * 100)
            : 0;

        navLeft.innerHTML = `
            <li><button class="contrast outline" style="padding: 4px 8px; border:none;" onclick="loadDashboard()">${ICONS.back}</button></li>
            <li><strong>${state.currentPath}</strong></li>
        `;
        navRight.innerHTML = `
            <li><button class="secondary outline" style="padding: 4px 8px;" onclick="handleGenerateCard()" title="Generate QR card">${ICONS.image} Generate QR card</button></li>
        `;
        
        let html = `
            <div class="dir-container">
                <section>
                    <div class="header-row">
                        <h2>Contents</h2>
                    </div>
                    ${uploadInProgress ? `
                    <article class="upload-progress-card">
                        <header>
                            <strong>Uploading ${Math.min(uploadState.totalFiles, uploadState.completedFiles + 1)} / ${uploadState.totalFiles}</strong>
                            <span>${uploadPercent}%</span>
                        </header>
                        <div class="upload-progress-meta">${uploadState.currentFileName}</div>
                        <progress value="${uploadedBytes}" max="${totalBytes}"></progress>
                        <small>${formatBytes(uploadedBytes)} / ${formatBytes(uploadState.totalBytes)}</small>
                    </article>
                    ` : ''}
                    <div class="file-list">
        `;

        const mp3s = state.files.filter(f => f.name.toLowerCase().endsWith('.mp3'));
        const others = state.files.filter(f => !f.name.toLowerCase().endsWith('.mp3'));

        const renderFileItem = (file, icon) => {
            const fileUrl = `${API_BASE}/file?path=${encodeURIComponent(state.currentPath)}&name=${encodeURIComponent(file.name)}`;
            return `
                <div class="file-item">
                    <span class="file-icon">${icon}</span>
                    <span class="file-name">${file.name}</span>
                    <a href="${fileUrl}" target="_blank" class="action-icon" title="Download">
                        ${ICONS.download}
                    </a>
                    <span class="action-icon" onclick="handleRename('${file.name}')" title="Rename">
                        ${ICONS.edit}
                    </span>
                    <span class="action-icon delete" onclick="handleDelete('${file.name}')" title="Delete">
                        ${ICONS.trash}
                    </span>
                </div>
            `;
        };

        if (mp3s.length > 0) {
            html += `<div class="file-item separator">Audio Files</div>`;
            mp3s.forEach(file => { html += renderFileItem(file, ICONS.music); });
        }

        if (others.length > 0) {
            html += `<div class="file-item separator">Other Files</div>`;
            others.forEach(file => { html += renderFileItem(file, ICONS.file); });
        }

        if (mp3s.length === 0 && others.length === 0) {
            html += `<div class="file-item" style="justify-content:center; color:var(--pico-muted-color);">Empty. Drag & drop or click below.</div>`;
        }

        html += `
                        <label id="drop-zone" class="file-item upload-item${uploadInProgress ? ' disabled' : ''}" style="border-top: 1px solid var(--pico-muted-border-color);">
                            <input type="file" multiple ${uploadInProgress ? 'disabled' : ''} onchange="handleUpload(this.files, 'file')">
                            <span class="file-icon">${ICONS.addFile}</span>
                            <span class="file-name">${uploadInProgress ? 'Upload in progress...' : 'Click or Drag & Drop to Upload...'}</span>
                        </label>
                    </div>
                </section>
        `;

        const coverFile = state.files.find(f => f.name.toLowerCase() === 'cover.jpg');
        const coverUrl = coverFile ? `${API_BASE}/file?path=${encodeURIComponent(state.currentPath)}&name=${encodeURIComponent(coverFile.name)}` : null;
        
        html += `
            <section>
                <div class="header-row">
                    <h2>Cover Image</h2>
                </div>
                <div class="cover-preview-card">
                    <div style="position:relative; margin:0 auto; max-width:100%;">
                        ${coverUrl ? `<img src="${coverUrl}">` : `<div class="placeholder-img" style="aspect-ratio:1/1; display:flex; align-items:center; justify-content:center; background:var(--pico-secondary-focus); color:var(--pico-secondary); border-radius:8px;">No Cover</div>`}
                        <label class="cover-edit-overlay${uploadInProgress ? ' disabled' : ''}" title="${uploadInProgress ? 'Upload in progress' : 'Change Cover'}">
                            <input type="file" accept="image/*" style="display:none" ${uploadInProgress ? 'disabled' : ''} onchange="handleUpload(this.files, 'cover')">
                            ${ICONS.edit}
                        </label>
                        ${coverUrl ? `
                        <div class="cover-delete-overlay" title="Delete Cover" onclick="handleDelete('cover.jpg')">
                            ${ICONS.trash}
                        </div>` : ''}
                    </div>
                    <p style="margin-top:0.5rem; font-size:0.8rem; color:var(--pico-muted-color);">Supports JPG, PNG, WEBP</p>
                </div>
            </section>
        `;
        
        html += `</div>`;
        app.innerHTML = html;
        setupDragAndDrop();
    }

    // Restore or fix scroll for logs
    if (state.view === 'debug-logs') {
        const newLogShell = document.querySelector('.debug-log-shell');
        if (newLogShell) {
            if (logsScroll) {
                if (logsScroll.atBottom) {
                    newLogShell.scrollTop = newLogShell.scrollHeight;
                } else {
                    newLogShell.scrollTop = logsScroll.top;
                }
            } else {
                // First load of the logs view, scroll to bottom to see newest logs
                newLogShell.scrollTop = newLogShell.scrollHeight;
            }
        }
    }
}

window.addEventListener('hashchange', navigate);
window.addEventListener('beforeunload', stopDeviceStatusRefresh);
window.onload = navigate;
