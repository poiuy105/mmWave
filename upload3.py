import requests, time, os, sys

base_url = "http://192.168.124.20"
frontend_dir = r"e:\Espidf\mmWave\frontend"

files = ["radar.js", "canvas.js", "style.css"]

for fname in files:
    fpath = os.path.join(frontend_dir, fname)
    if not os.path.exists(fpath):
        print(f"SKIP {fname} (not found)", flush=True)
        continue

    with open(fpath, 'rb') as f:
        data = f.read()

    url = f"{base_url}/api/files/upload?path=/storage/www/{fname}"
    
    try:
        resp = requests.post(url, data=data, headers={'Content-Type': 'application/octet-stream'}, timeout=20)
        result = resp.json()
        print(f"OK {fname} ({len(data)} bytes) -> {result.get('message', '')}", flush=True)
    except requests.exceptions.Timeout:
        print(f"FAIL {fname}: timeout", flush=True)
    except Exception as e:
        print(f"FAIL {fname}: {e}", flush=True)
    
    time.sleep(0.3)

print("\nDone!", flush=True)