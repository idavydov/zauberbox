/**
 * Lucide Icons (ISC License)
 * Copyright (c) 2022 Lucide Contributors
 * Copyright (c) 2013-2022 Cole Bemis (part of Feather Icons, MIT)
 */

const API_BASE = '/api';
let state = {
    view: 'dashboard',
    currentPath: null,
    directories: [],
    files: [],
    loading: true,
    selectionMode: false,
    selectedDirs: new Set()
};

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
    try {
        const response = await fetch(`${API_BASE}${endpoint}`, options);
        if (!response.ok) throw new Error();
        return await response.json();
    } catch (err) {
        if (endpoint === '/list') return MOCK_DATA;
        if (endpoint.startsWith('/files')) {
            const dir = state.directories.find(d => d.name === state.currentPath);
            return [
                { name: "01_intro.mp3", type: "audio/mpeg" },
                { name: "02_story.mp3", type: "audio/mpeg" },
                { name: dir?.cover || "cover.jpg", type: "image/jpeg" }
            ];
        }
        return { success: true };
    }
}

async function navigate() {
    const hash = window.location.hash;
    if (hash.startsWith('#/dir/')) {
        const path = decodeURIComponent(hash.substring(6));
        await enterDirectory(path, false);
    } else {
        await loadDashboard(false);
    }
}

async function loadDashboard(push = true) {
    if (push) window.location.hash = '/';
    state.loading = true; render();
    state.directories = await fetchAPI('/list');
    state.view = 'dashboard';
    state.loading = false; render();
}

async function enterDirectory(path, push = true) {
    if (push) window.location.hash = `/dir/${encodeURIComponent(path)}`;
    state.loading = true;
    state.currentPath = path; render();
    state.files = await fetchAPI(`/files?path=${encodeURIComponent(path)}`);
    if (state.directories.length === 0) {
        state.directories = await fetchAPI('/list');
    }
    state.view = 'directory';
    state.loading = false; render();
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
    if (!files || files.length === 0) return;
    state.loading = true; render();
    
    for (let file of files) {
        const type = forcedType || 'file';
        const lastModifiedMs = typeof file.lastModified === 'number' && file.lastModified > 0
            ? file.lastModified
            : Date.now();
        
        if (type === 'cover') {
            file = await convertToJpeg(file);
        }

        const formData = new FormData();
        formData.append('file', file);
        await fetchAPI(
            `/upload?path=${encodeURIComponent(state.currentPath)}&type=${encodeURIComponent(type)}&last_modified_ms=${encodeURIComponent(String(lastModifiedMs))}`,
            {
            method: 'POST',
            body: formData
            }
        );
    }
    
    state.directories = await fetchAPI('/list');
    enterDirectory(state.currentPath);
}

async function generateSingleCard(ctx, dirName, coverUrl, mp3s, xOffset, yOffset, width, height) {
    const QR_WIDTH_RATIO = 0.33;
    const QR_SECTION_WIDTH = Math.round(width * QR_WIDTH_RATIO);
    const COVER_WIDTH = width - QR_SECTION_WIDTH;

    const img = new Image();
    img.crossOrigin = "anonymous";
    await new Promise((resolve, reject) => {
        img.onload = resolve;
        img.onerror = () => reject(new Error(`Failed to load cover: ${dirName}`));
        img.src = coverUrl;
    });

    const imgAspect = img.width / img.height;
    const targetAspect = COVER_WIDTH / height;
    let sx, sy, sWidth, sHeight;

    if (imgAspect > targetAspect) {
        sHeight = img.height;
        sWidth = img.height * targetAspect;
        sx = (img.width - sWidth) / 2;
        sy = 0;
    } else {
        sWidth = img.width;
        sHeight = img.width / targetAspect;
        sx = 0;
        sy = (img.height - sHeight) / 2;
    }
    ctx.drawImage(img, sx, sy, sWidth, sHeight, xOffset, yOffset, COVER_WIDTH, height);

    const colors = getDominantColors(ctx, width, height, xOffset, yOffset);
    const noiseCanvas = document.createElement('canvas');
    noiseCanvas.width = 5;
    noiseCanvas.height = 5;
    const noiseCtx = noiseCanvas.getContext('2d');
    for (let y = 0; y < 5; y++) {
        for (let x = 0; x < 5; x++) {
            noiseCtx.fillStyle = colors[Math.floor(Math.random() * colors.length)];
            noiseCtx.fillRect(x, y, 1, 1);
        }
    }

    ctx.save();
    if (ctx.filter !== undefined) {
        ctx.filter = `blur(${QR_SECTION_WIDTH / 4}px)`;
    }
    ctx.drawImage(noiseCanvas, xOffset + COVER_WIDTH - 10, yOffset - 10, QR_SECTION_WIDTH + 20, height + 20);
    ctx.restore();

    const uri = `file://${dirName}`;
    const qrSize = Math.min(QR_SECTION_WIDTH, height) * 0.8;
    const qrContainer = document.createElement('div');
    new QRCode(qrContainer, {
        text: uri,
        width: qrSize,
        height: qrSize,
        colorDark : "#000000",
        colorLight : "#ffffff",
        correctLevel : QRCode.CorrectLevel.H
    });

    await new Promise(resolve => setTimeout(resolve, 50));
    const qrCanvas = qrContainer.querySelector('canvas');
    const qrX = xOffset + COVER_WIDTH + (QR_SECTION_WIDTH - qrSize) / 2;
    const qrY = yOffset + (height - qrSize) / 2;
    ctx.drawImage(qrCanvas, qrX, qrY);

    if (mp3s.length > 0) {
        const fontSize = 24;
        ctx.font = `bold ${fontSize}px sans-serif`;
        ctx.textBaseline = 'bottom';
        const margin = 15;
        const lineHeight = fontSize + 4;
        const maxTracks = Math.floor((height - (margin * 2)) / lineHeight);
        const visibleTracks = mp3s.slice(0, maxTracks);
        const totalTextHeight = visibleTracks.length * lineHeight;
        let currentY = yOffset + height - margin - totalTextHeight + fontSize;

        visibleTracks.forEach(track => {
            drawTextWithOutline(ctx, track, xOffset + margin, currentY);
            currentY += lineHeight;
        });
    }
}

async function handleGenerateCard() {
    const currentDir = state.directories.find(d => d.name === state.currentPath);
    const coverUrl = state.files.find(f => f.name.toLowerCase() === 'cover.jpg')
        ? `${API_BASE}/file?path=${encodeURIComponent(state.currentPath)}&name=${encodeURIComponent('cover.jpg')}`
        : null;

    if (!coverUrl) {
        await showModal({ title: 'Error', message: 'Please upload a cover image first.', confirmText: 'OK', showCancel: false });
        return;
    }

    state.loading = true; render();

    try {
        const DPI = 300;
        const MM_TO_INCH = 25.4;
        const WIDTH_PX = Math.round((76 / MM_TO_INCH) * DPI);
        const HEIGHT_PX = Math.round((34 / MM_TO_INCH) * DPI);

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
        link.download = `card_${state.currentPath}.jpg`;
        link.href = dataUrl;
        link.click();

    } catch (err) {
        await showModal({ title: 'Error', message: 'Failed to generate card.', confirmText: 'OK', showCancel: false });
    } finally {
        state.loading = false; render();
    }
}

async function handleDownloadSheets() {
    if (state.selectedDirs.size === 0) return;
    state.loading = true; render();
    try {
        const DPI = 300;
        const MM_TO_INCH = 25.4;
        const CARD_W_PX = Math.round((76 / MM_TO_INCH) * DPI);
        const CARD_H_PX = Math.round((34 / MM_TO_INCH) * DPI);
        const WIDTH_PX = Math.round((152 / MM_TO_INCH) * DPI);
        const HEIGHT_PX = Math.round((102 / MM_TO_INCH) * DPI);

        const selectedNames = Array.from(state.selectedDirs);
        for (let i = 0; i < selectedNames.length; i += 6) {
            const chunk = selectedNames.slice(i, i + 6);
            const canvas = document.createElement('canvas');
            canvas.width = WIDTH_PX; canvas.height = HEIGHT_PX;
            const ctx = canvas.getContext('2d');
            ctx.fillStyle = 'white'; ctx.fillRect(0, 0, WIDTH_PX, HEIGHT_PX);

            for (let j = 0; j < chunk.length; j++) {
                const name = chunk[j];
                const files = await fetchAPI(`/files?path=${encodeURIComponent(name)}`);
                const coverUrl = `${API_BASE}/file?path=${encodeURIComponent(name)}&name=${encodeURIComponent('cover.jpg')}`;
                const mp3s = files.filter(f => f.name.toLowerCase().endsWith('.mp3')).map(f => f.name.replace(/\.mp3$/i, '')).sort();
                await generateSingleCard(ctx, name, coverUrl, mp3s, (j % 2) * CARD_W_PX, Math.floor(j / 2) * CARD_H_PX, CARD_W_PX, CARD_H_PX);
            }

            // Add 0.5mm white lines between cards
            const SEPARATOR_PX = Math.round((0.5 / MM_TO_INCH) * DPI);
            ctx.strokeStyle = 'white';
            ctx.lineWidth = SEPARATOR_PX;
            
            // Vertical separator
            ctx.beginPath();
            ctx.moveTo(CARD_W_PX, 0);
            ctx.lineTo(CARD_W_PX, HEIGHT_PX);
            ctx.stroke();

            // Horizontal separators
            ctx.beginPath();
            ctx.moveTo(0, CARD_H_PX);
            ctx.lineTo(WIDTH_PX, CARD_H_PX);
            ctx.stroke();
            
            ctx.beginPath();
            ctx.moveTo(0, CARD_H_PX * 2);
            ctx.lineTo(WIDTH_PX, CARD_H_PX * 2);
            ctx.stroke();

            // Add 1mm white frame around the sheet
            const FRAME_PX = Math.round((1 / MM_TO_INCH) * DPI);
            ctx.strokeStyle = 'white';
            ctx.lineWidth = FRAME_PX * 2;
            ctx.strokeRect(0, 0, WIDTH_PX, HEIGHT_PX);

            const link = document.createElement('a');
            link.download = `sheet_${Math.floor(i/6) + 1}.jpg`;
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

function getDominantColors(ctx, width, height, xOffset = 0, yOffset = 0) {
    const data = ctx.getImageData(xOffset, yOffset, width, height).data;
    const sampleCount = 100;
    const colors = [];
    for (let i = 0; i < sampleCount; i++) {
        const idx = Math.floor(Math.random() * (data.length / 4)) * 4;
        colors.push(`rgb(${data[idx]}, ${data[idx+1]}, ${data[idx+2]})`);
    }
    return colors;
}

function drawTextWithOutline(ctx, text, x, y) {
    ctx.strokeStyle = 'black';
    ctx.lineWidth = 3;
    ctx.lineJoin = 'round';
    ctx.strokeText(text, x, y);
    ctx.fillStyle = 'white';
    ctx.fillText(text, x, y);
}

async function convertToJpeg(file) {
    return new Promise((resolve) => {
        const reader = new FileReader();
        reader.onload = (e) => {
            const img = new Image();
            img.onload = () => {
                const canvas = document.createElement('canvas');
                canvas.width = img.width;
                canvas.height = img.height;
                const ctx = canvas.getContext('2d');
                ctx.drawImage(img, 0, 0);
                canvas.toBlob((blob) => {
                    resolve(new File([blob], "cover.jpg", { type: 'image/jpeg' }));
                }, 'image/jpeg', 0.9);
            };
            img.src = e.target.result;
        };
        reader.readAsDataURL(file);
    });
}

function setupDragAndDrop() {
    const zone = document.getElementById('drop-zone');
    if (!zone) return;

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
    
    if (state.loading) {
        app.innerHTML = '<div aria-busy="true" id="loading">Working...</div>';
        return;
    }

    if (state.view === 'dashboard') {
        navLeft.innerHTML = `<li><strong>Zauberbox</strong></li>`;
        navRight.innerHTML = `<li><button class="${state.selectionMode ? 'primary' : 'contrast outline'}" style="padding: 4px 8px;" onclick="toggleSelectionMode()">${state.selectionMode ? 'Cancel' : 'Select'}</button></li>`;
        
        let html = `<div class="${state.selectionMode ? 'selection-mode' : ''}"><h2>Directories</h2><div class="dir-grid">`;
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
    } else {
        navLeft.innerHTML = `
            <li><button class="contrast outline" style="padding: 4px 8px; border:none;" onclick="loadDashboard()">${ICONS.back}</button></li>
            <li><strong>${state.currentPath}</strong></li>
        `;
        navRight.innerHTML = `
            <li><button class="secondary outline" style="padding: 4px 8px;" onclick="handleGenerateCard()" title="Generate Card Image">${ICONS.image} Card</button></li>
        `;
        
        let html = `
            <div class="dir-container">
                <section>
                    <div class="header-row">
                        <h2>Contents</h2>
                    </div>
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
                        <label id="drop-zone" class="file-item upload-item" style="border-top: 1px solid var(--pico-muted-border-color);">
                            <input type="file" multiple onchange="handleUpload(this.files, 'file')">
                            <span class="file-icon">${ICONS.plus}</span>
                            <span class="file-name">Click or Drag & Drop to Upload...</span>
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
                        <label class="cover-edit-overlay" title="Change Cover">
                            <input type="file" accept="image/*" style="display:none" onchange="handleUpload(this.files, 'cover')">
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
}

window.addEventListener('hashchange', navigate);
window.onload = navigate;
