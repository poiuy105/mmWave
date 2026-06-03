---
name: "esp32-upload-server"
description: "ESP32 HTTP file upload server with static file serving. Invoke when developing, debugging, or modifying the ESP32 web server's upload page, file management API, static file handler, or URI handler registration order."
---

# ESP32 Upload Server Skill

ESP-IDF HTTP server with file upload, static file serving, and WebSocket support for the mmWave radar monitor project.

## Architecture

```
main/web_server/
├── http_server.c              # Main server: uri_handlers[], http_server_start/stop
├── http_server.h
├── fatfs_init.c               # FATFS mount at /storage, create /storage/www /storage/logs /storage/config
├── file_manager.c             # File operations: upload, list, delete, fs_info
├── upload_page.h              # Embedded HTML/JS upload page (inline string)
├── embedded_web.h             # Embedded index.html dashboard (inline string)
├── core/
│   ├── http_server_core.c     # httpd_config_t setup, httpd_start/stop
│   └── server_context.c       # Global server context (config, stats, mutex)
├── handlers/
│   ├── upload_handler.c       # /upload page + /api/files/* + /api/fs/* handlers
│   ├── health_handler.c       # /api/health, /api/ready, /api/live
│   └── (websocket, security)
├── config/
│   └── server_config.c        # server_config_t defaults + Kconfig overrides
└── websocket/
    └── ws_server.c            # /ws WebSocket handler
```

## URI Handler Registration Order (CRITICAL)

ESP-IDF HTTPD with `httpd_uri_match_wildcard` matches handlers in **registration order**.
The `/*` wildcard MUST be registered LAST, otherwise it intercepts all requests.

### Registration sequence in `http_server_start()`:

```
Step 5: uri_handlers[] array (exact matches)
  ├── GET  /                 → static_file_handler
  ├── GET  /index.html      → static_file_handler
  ├── GET  /upload          → upload_page_handler
  ├── GET  /api/status      → api_status_handler
  ├── GET  /api/system/info → api_system_info_handler
  └── OPTIONS /api/*         → api_options_handler

Step 6: health_handler_register()
  ├── GET  /api/health      → api_health_handler
  ├── GET  /api/ready       → api_ready_handler
  └── GET  /api/live        → api_live_handler

Step 7: upload_handler_register()
  ├── POST /api/files/upload  → api_upload_handler
  ├── GET  /api/files/list    → api_file_list_handler
  ├── DELETE /api/files/delete → api_file_delete_handler
  ├── GET  /api/fs/info       → api_fs_info_handler
  └── POST /api/fs/format     → api_fs_format_handler

Step 8: ws_server_create()
  └── GET  /ws               → ws_handler (upgraded to WebSocket)

Step 9: /* wildcard (LAST)
  └── GET  /*                → static_file_handler
```

### Key Rules:
1. **Exact matches before wildcards** — always
2. **`/*` must be registered LAST** — after health, upload, ws handlers
3. **Handler registration must handle `ESP_ERR_HTTPD_HANDLER_EXISTS`** gracefully (crash reboot resilience)
4. **`config.uri_match_fn = httpd_uri_match_wildcard`** must be set in `http_server_core.c`

## API Reference

### Upload Page APIs (called by upload_page.h frontend JS)

| Method | URI | Handler | Description |
|--------|-----|---------|-------------|
| GET | `/upload` | `upload_page_handler` | Returns embedded HTML upload page |
| GET | `/api/fs/info` | `api_fs_info_handler` | Storage info: total_bytes, used_bytes, free_bytes, file_count, dir_count |
| GET | `/api/files/list?path=/storage/www` | `api_file_list_handler` | List files in directory, returns `{path, files[{name,path,size,is_dir}], count}` |
| POST | `/api/files/upload?path=/storage/www/filename` | `api_upload_handler` | Upload file (body = binary content, Content-Type: application/octet-stream) |
| DELETE | `/api/files/delete?path=/storage/www/filename` | `api_file_delete_handler` | Delete file, returns `{success, message}` |
| POST | `/api/fs/format` | `api_fs_format_handler` | Format storage partition, returns `{success, message}` |

### System APIs

| Method | URI | Handler | Description |
|--------|-----|---------|-------------|
| GET | `/api/status` | `api_status_handler` | Server status: `{status, server, version, websocket_clients}` |
| GET | `/api/system/info` | `api_system_info_handler` | System info: `{chip_model, free_heap, wifi_ssid, ip}` |
| GET | `/api/health` | `api_health_handler` | Health check with component status |
| GET | `/api/ready` | `api_ready_handler` | Readiness check (WiFi + FATFS + radar) |
| GET | `/api/live` | `api_live_handler` | Liveness check: `{alive: true}` |
| OPTIONS | `/api/*` | `api_options_handler` | CORS preflight |

### Static Files

| Method | URI | Handler | Description |
|--------|-----|---------|-------------|
| GET | `/` | `static_file_handler` | Redirects to `/index.html` |
| GET | `/index.html` | `static_file_handler` | Serves `/storage/www/index.html` |
| GET | `/*` | `static_file_handler` | Serves files from `/storage/www/` + URI |

### WebSocket

| Method | URI | Handler | Description |
|--------|-----|---------|-------------|
| GET | `/ws` | `ws_handler` | WebSocket for real-time radar data |

## Static File Handler Details

```c
// Path construction (hardcoded to bypass config copy issue):
snprintf(filepath, sizeof(filepath), "/storage/www%s", uri);

// Root path redirect:
if (strcmp(uri, "/") == 0) uri = "/index.html";

// MIME types: .html→text/html, .css→text/css, .js→application/javascript,
//             .json→application/json, .png→image/png, .jpg→image/jpeg, .ico→image/x-icon
```

## HTTPD Config (http_server_core.c)

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.uri_match_fn = httpd_uri_match_wildcard;  // REQUIRED for /* support
config.lru_purge_enable = true;                   // Auto-close idle connections
config.max_uri_handlers = 32;
config.max_open_sockets = 7;
```

## Common Pitfalls & Fixes

### 1. "Nothing matches the given URI"
- **Cause**: Handler not registered, or `/*` wildcard intercepted the request
- **Fix**: Ensure `config.uri_match_fn = httpd_uri_match_wildcard` and `/*` is registered LAST

### 2. `httpd_resp_send_404()` shows "Nothing matches"
- **Cause**: Returning `ESP_FAIL` after `httpd_resp_send_404()` causes HTTPD to send a second error
- **Fix**: Return `ESP_OK` after any `httpd_resp_send_*()` call (response already sent)

### 3. `ESP_ERR_HTTPD_HANDLER_EXISTS` on reboot
- **Cause**: Crash reboot leaves HTTPD global state with old handlers
- **Fix**: Check `reg_ret == ESP_ERR_HTTPD_HANDLER_EXISTS` and skip gracefully

### 4. Config values empty after `server_context_init()`
- **Cause**: `ctx->config->fatfs_mount_path` / `static_file_root` may be empty due to memcpy issues
- **Fix**: Hardcode `/storage/www` in static_file_handler as fallback

### 5. Upload page `webkitdirectory` adds subfolder path
- **Cause**: `<input webkitdirectory>` includes folder name in `file.name`
- **Fix**: Use `multiple` attribute only (no `webkitdirectory`)

## FATFS Configuration (sdkconfig.defaults)

```
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
CONFIG_FATFS_API_ENCODING_UTF_8=y
```

Mount config: `allocation_unit_size = 4KB`, `format_if_mount_failed = true`, `max_files = 16`

## File Upload Flow

1. Frontend: `FileReader.readAsArrayBuffer(file)` → `XMLHttpRequest.send()`
2. Backend: `api_upload_handler` → `file_manager_ensure_dir()` → `fopen()` → streaming write
3. Storage path: `/storage/www/<filename>`
4. Response: `{"success":true,"message":"File uploaded successfully","path":"...","bytes_written":N}`
