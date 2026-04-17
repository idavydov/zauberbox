from flask import Flask, request, send_from_directory, send_file, jsonify
import os
import shutil
import argparse
import time
from werkzeug.utils import secure_filename

app = Flask(__name__)

# Server start time for dynamic logs
START_TIME = time.time()
DEBUG_BATTERY_OVERRIDE = {
    "enabled": False,
    "target_mv": 0,
    "activate_at": 0.0,
}

# Configurable storage directory
STORAGE_DIR = "mock_sd"
# Project root is 4 levels up from this script (src/zauberbox/web/mock_server.py)
PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(PACKAGE_DIR, "..", "..", ".."))
# Web files are in the 'web' directory at project root
WEB_DIR = os.path.join(PROJECT_ROOT, "web")

@app.route('/')
def index():
    return send_from_directory(WEB_DIR, 'index.html')

@app.route('/<path:filename>')
def serve_static(filename):
    if filename in ['style.css', 'app.js', 'pico.min.css', 'qrcode.min.js','favicon.png']:
        return send_from_directory(WEB_DIR, filename)
    return "", 404

@app.route('/api/list')
def list_directories():
    dirs = []
    if not os.path.exists(STORAGE_DIR):
        os.makedirs(STORAGE_DIR, exist_ok=True)
        
    for name in sorted(os.listdir(STORAGE_DIR)):
        path = os.path.join(STORAGE_DIR, name)
        if os.path.isdir(path):
            cover = None
            if os.path.exists(os.path.join(path, "cover.jpg")):
                cover = f"/api/file?path={name}&name=cover.jpg"
            
            first_mp3 = None
            files = sorted(os.listdir(path))
            for f in files:
                if f.lower().endswith(".mp3"):
                    first_mp3 = f
                    break
            
            dirs.append({
                "name": name,
                "cover": cover,
                "first_mp3": first_mp3
            })
    return jsonify(dirs)

@app.route('/api/files')
def list_files():
    path = request.args.get('path', '')
    full_path = os.path.join(STORAGE_DIR, path)
    if not os.path.exists(full_path) or not os.path.isdir(full_path):
        return jsonify([])
    
    files = []
    for name in sorted(os.listdir(full_path)):
        files.append({
            "name": name,
            "type": "audio/mpeg" if name.lower().endswith(".mp3") else "image/jpeg" if name.lower().endswith(".jpg") else "application/octet-stream"
        })
    return jsonify(files)

@app.route('/api/file')
def get_file():
    path = request.args.get('path', '')
    name = request.args.get('name', '')
    return send_from_directory(os.path.join(STORAGE_DIR, path), name)

@app.route('/api/debug/camera-frame')
def debug_camera_frame():
    image_path = os.path.join(PROJECT_ROOT, "img", "photo1.jpg")
    if not os.path.exists(image_path):
        return jsonify({"success": False, "error": "Mock preview image missing"}), 503
    return send_file(image_path, mimetype='image/jpeg', max_age=0)

@app.route('/api/debug/camera-preview/start', methods=['POST'])
def debug_camera_preview_start():
    return jsonify({"success": True})

@app.route('/api/debug/camera-preview/stop', methods=['POST'])
def debug_camera_preview_stop():
    return jsonify({"success": True})

@app.route('/api/debug/logs')
def debug_logs():
    body = """[101] [12034 ms] Initializing Zauberbox...
[102] [12640 ms] App state: Boot -> QrScan (wifi=Disabled)
[103] [12642 ms] QR service: startScanning requested in state=QrScan camera=0 preview=0 nextRetryAt=0.
[104] [12758 ms] QR service: OV5640 camera initialized (decoder=1).
[105] [12760 ms] QR service: scanner active.
"""
    # Append dynamic lines every 5 seconds
    elapsed = int(time.time() - START_TIME)
    steps = elapsed // 5
    for i in range(1, steps + 1):
        ts = 13000 + i * 5000
        line_num = 105 + i
        body += f"[{line_num}] [{ts} ms] Mock event: heartbeat tick {i}...\n"
    
    # Keep it manageable
    lines = body.splitlines()
    if len(lines) > 50:
        body = "\n".join(lines[-50:]) + "\n"

    return app.response_class(body, mimetype='text/plain')

@app.route('/api/debug/battery-override', methods=['POST'])
def debug_battery_override_set():
    data = request.get_json(silent=True) or {}
    target_mv = int(data.get('voltage_mv', 0))
    delay_ms = int(data.get('delay_ms', 0))
    if target_mv < 3000 or target_mv > 4500:
        return jsonify({"success": False, "error": "voltage_mv must be between 3000 and 4500"}), 400
    if delay_ms < 0 or delay_ms > 600000:
        return jsonify({"success": False, "error": "delay_ms must be between 0 and 600000"}), 400

    DEBUG_BATTERY_OVERRIDE["enabled"] = True
    DEBUG_BATTERY_OVERRIDE["target_mv"] = target_mv
    DEBUG_BATTERY_OVERRIDE["activate_at"] = time.time() + (delay_ms / 1000.0)
    return jsonify({"success": True})

@app.route('/api/debug/battery-override', methods=['DELETE'])
def debug_battery_override_clear():
    DEBUG_BATTERY_OVERRIDE["enabled"] = False
    DEBUG_BATTERY_OVERRIDE["target_mv"] = 0
    DEBUG_BATTERY_OVERRIDE["activate_at"] = 0.0
    return jsonify({"success": True})

@app.route('/api/status')
def status():
    now = time.time()
    override_enabled = DEBUG_BATTERY_OVERRIDE["enabled"]
    override_active = override_enabled and now >= DEBUG_BATTERY_OVERRIDE["activate_at"]
    voltage_mv = DEBUG_BATTERY_OVERRIDE["target_mv"] if override_active else 3992
    percent = 84
    is_low = False
    is_critical = False

    if override_active:
        if voltage_mv <= 3300:
            percent = 0
        elif voltage_mv >= 4200:
            percent = 100
        else:
            percent = round((voltage_mv - 3300) * 100 / (4200 - 3300))
        is_low = voltage_mv <= 3600
        is_critical = voltage_mv <= 3450

    return jsonify({
        "app_state": "QrScan",
        "wifi_mode": "Connected",
        "battery": {
            "initialized": True,
            "has_reading": True,
            "reading_available": True,
            "reading_stable": True,
            "availability": "available",
            "adc_mv": 1318,
            "voltage_mv": voltage_mv,
            "voltage_v": voltage_mv / 1000.0,
            "percent": percent,
            "is_low": is_low,
            "is_critical": is_critical,
            "updated_at_ms": 18420,
            "debug_override": {
                "enabled": override_enabled,
                "active": override_active,
                "target_mv": DEBUG_BATTERY_OVERRIDE["target_mv"] if override_enabled else 0,
                "target_v": (DEBUG_BATTERY_OVERRIDE["target_mv"] / 1000.0) if override_enabled else 0.0,
                "activate_at_ms": int((DEBUG_BATTERY_OVERRIDE["activate_at"] - START_TIME) * 1000) if override_enabled else 0,
                "activate_in_ms": max(0, int((DEBUG_BATTERY_OVERRIDE["activate_at"] - now) * 1000)) if override_enabled and not override_active else 0,
            }
        }
    })

@app.route('/api/mkdir', methods=['POST'])
def mkdir():
    data = request.json
    name = secure_filename(data.get('name'))
    os.makedirs(os.path.join(STORAGE_DIR, name), exist_ok=True)
    return jsonify({"success": True})

@app.route('/api/rmdir', methods=['POST'])
def rmdir():
    data = request.json
    name = secure_filename(data.get('name'))
    path = os.path.join(STORAGE_DIR, name)
    if os.path.exists(path) and os.path.isdir(path):
        shutil.rmtree(path)
    return jsonify({"success": True})

@app.route('/api/upload', methods=['POST'])
def upload():
    if 'file' not in request.files:
        return jsonify({"success": False, "error": "No file"}), 400
    
    file = request.files['file']
    path = request.args.get('path', request.form.get('path', ''))
    upload_type = request.args.get('type', request.form.get('type', ''))
    
    target_dir = os.path.join(STORAGE_DIR, path)
    os.makedirs(target_dir, exist_ok=True)
    
    if upload_type == 'cover':
        filename = "cover.jpg"
    else:
        filename = secure_filename(file.filename)
    
    file.save(os.path.join(target_dir, filename))
    return jsonify({"success": True})

@app.route('/api/delete', methods=['POST'])
def delete_file():
    data = request.json
    path = data.get('path', '')
    file_name = data.get('file_name', '')
    full_path = os.path.join(STORAGE_DIR, path, file_name)
    if os.path.exists(full_path):
        os.remove(full_path)
    return jsonify({"success": True})

@app.route('/api/rename', methods=['POST'])
def rename_file():
    data = request.json
    path = data.get('path', '')
    old_name = data.get('old_name', '')
    new_name = secure_filename(data.get('new_name', ''))
    
    old_path = os.path.join(STORAGE_DIR, path, old_name)
    new_path = os.path.join(STORAGE_DIR, path, new_name)
    
    if os.path.exists(old_path):
        os.rename(old_path, new_path)
    return jsonify({"success": True})

def main():
    global STORAGE_DIR
    parser = argparse.ArgumentParser(description="Mock ESP32 Web Server")
    parser.add_argument("--root", default="mock_sd", help="Directory for the mock storage")
    parser.add_argument("--port", type=int, default=5000, help="Port to run the server on")
    args = parser.parse_args()
    
    STORAGE_DIR = os.path.abspath(args.root)
    print(f"Mock server starting. Storage: {STORAGE_DIR}")
    print(f"Web source directory: {WEB_DIR}")
    
    if not os.path.exists(STORAGE_DIR):
        os.makedirs(STORAGE_DIR)
        
    app.run(debug=True, port=args.port)

if __name__ == '__main__':
    main()
