/**
 * Lucide Icons (ISC License)
 * Copyright (c) 2022 Lucide Contributors
 * Copyright (c) 2013-2022 Cole Bemis (part of Feather Icons, MIT)
 */

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
    debugLogsText: '',
    debugLogsLoading: false,
    debugLogsError: '',
    debugLogsUpdatedAt: 0,
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

const ICONS = {
    music: `<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"></path><circle cx="6" cy="18" r="3"></circle><circle cx="18" cy="16" r="3"></circle></svg>`,
    edit: `<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path></svg>`,
    trash: `<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="3 6 5 6 21 6"></polyline><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path><line x1="10" y1="11" x2="10" y2="17"></line><line x1="14" y1="11" x2="14" y2="17"></line></svg>`,
    plus: `<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>`,
    back: `<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="19" y1="12" x2="5" y2="12"></line><polyline points="12 19 5 12 12 5"></polyline></svg>`,
    image: `<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><circle cx="8.5" cy="8.5" r="1.5"></circle><polyline points="21 15 16 10 5 21"></polyline></svg>`,
    file: `<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"></path><polyline points="13 2 13 9 20 9"></polyline></svg>`,
    download: `<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>`,
    check: `<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>`
};

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
    if (deviceStatusTimerId) {
        window.clearTimeout(deviceStatusTimerId);
    }
    deviceStatusTimerId = window.setTimeout(() => {
        deviceStatusTimerId = 0;
        refreshDeviceStatus();
    }, delayMs);
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

async function refreshDebugPreview() {
    if (state.view !== 'debug-camera' || debugPreviewAbortController) {
        return;
    }

    state.debugPreviewLoading = true;
    render();

    const controller = new AbortController();
    debugPreviewAbortController = controller;
    try {
        const response = await fetch(`${API_BASE}/debug/camera-frame?ts=${Date.now()}`, {
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
        if (!currentHash.startsWith('#/debug')) {
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

async function openDebugCamera(push = true) {
    if (push) {
        const currentHash = window.location.hash || '#/';
        if (!currentHash.startsWith('#/debug')) {
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
        if (!currentHash.startsWith('#/debug')) {
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

function leaveDebugMenu() {
    if (state.view === 'debug-camera' || state.view === 'debug-logs') {
        window.location.hash = '/debug';
        return;
    }
    const targetHash = debugReturnHash && debugReturnHash !== '#/debug'
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
    } else if (hash === '#/debug') {
        openDebug(false);
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
        const batteryStateClass = battery?.is_critical
            ? 'critical'
            : battery?.is_low
                ? 'low'
                : '';
        const batteryDetail = state.deviceStatusError || 'Voltage-based estimate. Power-source detection is not wired yet.';

        navLeft.innerHTML = `<li><strong>Zauberbox</strong></li>`;
        navRight.innerHTML = `
            <li class="nav-battery ${batteryStateClass}" title="${escapeHtml(batteryDetail)}">${escapeHtml(batterySummary)}</li>
            <li><button class="${state.selectionMode ? 'primary' : 'contrast outline'}" style="padding: 4px 8px;" onclick="toggleSelectionMode()">${state.selectionMode ? 'Cancel' : 'Select'}</button></li>
        `;

        let html = `<div class="${state.selectionMode ? 'selection-mode' : ''}">`;
        html += `<h2>Directories</h2><div class="dir-grid">`;
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
                    ${ICONS.plus}
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
            <li><button class="secondary outline" style="padding: 4px 8px;" onclick="handleGenerateCard()" title="Generate Tile Image">${ICONS.image} Tile</button></li>
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
                            <span class="file-icon">${ICONS.plus}</span>
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
