
def estimate_arduino_json_6(structure):
    """
    Estimates the memory usage of ArduinoJson 6 on a 32-bit platform (like ESP32).
    Based on: https://arduinojson.org/v6/assistant/
    
    Rules for 32-bit:
    - Variant: 16 bytes
    - Object: 16 bytes + (16 bytes * number of keys)
    - Array: 16 bytes + (16 bytes * number of elements)
    - Strings: If not const char*, they are stored in the pool.
    """
    overhead = 0
    string_pool = 0
    
    if isinstance(structure, dict):
        # Object overhead: 16 bytes base + 16 per key
        overhead += 16 + (16 * len(structure))
        for key, value in structure.items():
            # Keys are usually copied to the pool if not const
            string_pool += len(key) + 1
            
            val_overhead, val_pool = estimate_arduino_json_6(value)
            overhead += val_overhead
            string_pool += val_pool
            
    elif isinstance(structure, list):
        # Array overhead: 16 bytes base + 16 per element
        overhead += 16 + (16 * len(structure))
        for item in structure:
            val_overhead, val_pool = estimate_arduino_json_6(item)
            overhead += val_overhead
            string_pool += val_pool
            
    elif isinstance(structure, str):
        # Strings are stored in the pool
        string_pool += len(structure) + 1
        
    elif isinstance(structure, bool):
        # Boolean is stored in the variant, no extra pool
        pass
        
    elif isinstance(structure, (int, float)):
        # Numbers are stored in the variant, no extra pool
        pass
        
    return overhead, string_pool

def print_estimate(name, structure):
    overhead, pool = estimate_arduino_json_6(structure)
    total = overhead + pool
    # ArduinoJson documentation recommends adding some slack and aligning to power of 2
    recommended = 1
    while recommended < total:
        recommended *= 2
        
    print(f"Estimation for {name}:")
    print(f"  Overhead:    {overhead} bytes")
    print(f"  String Pool: {pool} bytes")
    print(f"  Total:       {total} bytes")
    print(f"  Recommended: {recommended} bytes (nearest power of 2)")
    print("-" * 30)

# 1. handleStatus structure
status_structure = {
    "app_state": "Playing",
    "app_state_display": "Playing",
    "wifi_mode": "Connected",
    "input": {
        "backend": "qr",
        "selection_state_label": "QR Scan",
        "selection_active": False,
        "hardware_active": True,
        "stops_before_playback": True,
        "capabilities": {
            "debug_camera_preview": True,
            "qr_album_cards": True
        }
    },
    "playback": {
        "mode": "playing",
        "has_album": True,
        "album_id": "001-some-album-name",
        "track_name": "01-very-long-track-name-that-might-happen.mp3",
        "track_index": 1,
        "track_count": 12,
        "position_seconds": 120,
        "duration_seconds": 180
    },
    "battery": {
        "initialized": True,
        "has_reading": True,
        "reading_available": True,
        "reading_stable": True,
        "availability": "available",
        "adc_mv": 1300,
        "voltage_mv": 3900,
        "voltage_v": 3.9,
        "percent": 85,
        "is_low": False,
        "is_critical": False,
        "updated_at_ms": 12345,
        "debug_override": {
            "enabled": True,
            "active": False,
            "target_mv": 3500,
            "target_v": 3.5,
            "activate_at_ms": 20000,
            "activate_in_ms": 5000
        }
    }
}

# 2. handleListAlbums structure (with 20 albums as a test case)
def make_albums_structure(count):
    albums = []
    for i in range(count):
        albums.append({
            "name": f"{i:03d}-album-name",
            "cover": f"/api/file?path={i:03d}-album-name&name=cover.jpg",
            "title": "Album Title Overridden in title.txt",
            "first_mp3": "01-first-track-name.mp3"
        })
    return albums

print_estimate("handleStatus", status_structure)
print_estimate("handleListAlbums (10 albums)", make_albums_structure(10))
print_estimate("handleListAlbums (20 albums)", make_albums_structure(20))
print_estimate("handleListAlbums (30 albums)", make_albums_structure(30))
print_estimate("handleListAlbums (100 albums)", make_albums_structure(100))
