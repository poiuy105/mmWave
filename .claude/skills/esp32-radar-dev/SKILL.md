---
name: "esp32-radar-dev"
description: "ESP-IDF millimeter wave radar driver development workflow. Invoke when user wants to develop radar drivers, parse radar PDF protocols, build radar libraries for ESP32, or push to GitHub for CI compilation."
---

# ESP32 Radar Driver Development Skill

This skill encapsulates the complete workflow for developing millimeter wave radar drivers on ESP-IDF, using the HLK-LD2460 as reference implementation.

## When to Invoke

- User wants to develop a new radar driver for ESP32
- User has radar PDF documentation that needs to be parsed and converted to code
- User wants to build/compile radar firmware via GitHub Actions CI
- User wants to add a new radar type to the existing radar component

## Workflow Overview

```
PDF Documentation → Protocol Analysis → MD Documentation → Driver Code → CI Build → Skill Update
```

## Step 1: Read Radar PDF Documentation

Use `pymupdf` (fitz) to extract text from radar PDF files:

```python
import fitz
doc = fitz.open(r'path/to/radar_protocol.pdf')
for i, page in enumerate(doc):
    print(f"=== Page {i+1} ===")
    print(page.get_text())
```

Key information to extract:
- UART parameters (baud rate, data bits, stop bits, parity)
- Frame format (header, function code, data length, payload, tail)
- Report/data output protocol (target data format, coordinate system)
- Command protocol (all function codes with send/ACK frames)
- Factory defaults
- Hardware specs (voltage, current, pin definitions)

## Step 2: Write Protocol MD Documentation

Create `docs/<RADAR_NAME>_protocol.md` with:

1. **Product Overview**: Features, electrical specs
2. **Pin Definitions**: UART TX/RX pins, power pins
3. **Coordinate System**: Axis ranges, angle conventions
4. **Communication Protocol**: Frame format, all function codes with hex examples
5. **Factory Defaults**: All default parameter values
6. **Usage Notes**: Power requirements, installation guidelines

## Step 3: Create/Update Radar Driver

### Directory Structure

```
components/radar/
├── CMakeLists.txt          # Conditional compilation based on Kconfig
├── Kconfig                 # menuconfig radar type selection
├── include/radar.h         # Unified header (compile-time macro mapping)
└── src/<radar_name>/
    ├── radar_<name>.h      # Data structures + API declarations
    └── radar_<name>.c      # Protocol parser + command implementation
```

### Driver Architecture

1. **State Machine Parser**: Parse binary frames byte-by-byte
   - Report frames: `F4 F3 F2 F1` header → parse targets → `F8 F7 F6 F5` tail
   - ACK frames: `FD FC FB FA` header → check result → `04 03 02 01` tail

2. **Command Builder**: Construct command frames with proper encoding
   - All data in little-endian format
   - Unit conversion (meters×100, degrees×10, etc.)

3. **Async UART Event-Driven**: FreeRTOS task + UART event queue + esp_event loop
   - `uart_driver_install()` with ring buffer
   - `xTaskCreate()` for parser task
   - `esp_event_post_to()` for application notification

4. **Command Synchronization**: Binary semaphore for ACK waiting
   - `xSemaphoreTake()` with timeout
   - Parse ACK result (success/fail) from response

### Key API Pattern

```c
// Lifecycle
radar_handle_t radar_<name>_init(const radar_<name>_config_t *config);
esp_err_t radar_<name>_deinit(radar_handle_t handle);

// Event registration
esp_err_t radar_<name>_add_handler(handle, event_handler, args);
esp_err_t radar_<name>_remove_handler(handle, event_handler);

// Report control
esp_err_t radar_<name>_enable_report(handle, bool enable);

// Configuration commands (set/get pairs)
esp_err_t radar_<name>_set_<param>(handle, ...);
esp_err_t radar_<name>_get_<param>(handle, ...);

// System
esp_err_t radar_<name>_restart(handle);
esp_err_t radar_<name>_factory_reset(handle);
esp_err_t radar_<name>_set_baud_rate(handle, index);
esp_err_t radar_<name>_get_firmware_version(handle, &version);
```

### Unified Header (radar.h)

Use compile-time macro mapping for zero-cost abstraction:

```c
#if defined(CONFIG_RADAR_LD2460)
    #include "radar_ld2460.h"
    typedef ld2460_config_t   radar_config_t;
    typedef ld2460_handle_t   radar_handle_t;
    #define RADAR_INIT(cfg)   ld2460_init(cfg)
    // ...
#elif defined(CONFIG_RADAR_NMEA)
    // ...
#endif
```

## Step 4: Update Build System

### CMakeLists.txt (components/radar/)

```cmake
if(CONFIG_RADAR_LD2460)
    set(RADAR_SRCS src/ld2460/radar_ld2460.c)
    set(RADAR_INCLUDES include src/ld2460)
elseif(CONFIG_RADAR_NMEA)
    set(RADAR_SRCS ${CMAKE_SOURCE_DIR}/main/nmea_parser.c)
    set(RADAR_INCLUDES include ${CMAKE_SOURCE_DIR}/main)
endif()

idf_component_register(SRCS ${RADAR_SRCS}
                       INCLUDE_DIRS ${RADAR_INCLUDES}
                       REQUIRES esp_event esp_driver_uart)
```

### Kconfig

```kconfig
choice RADAR_TYPE
    prompt "Select radar type"
    default RADAR_LD2460
    config RADAR_LD2460
        bool "HLK-LD2460"
    config RADAR_NMEA
        bool "NMEA Radar"
endchoice
```

### sdkconfig.defaults

```
CONFIG_RADAR_LD2460=y
CONFIG_RADAR_UART_TX_PIN=1
CONFIG_RADAR_UART_RX_PIN=2
```

## Step 5: GitHub Actions CI Build

Use `espressif/esp-idf-ci-action@v1`:

```yaml
- uses: espressif/esp-idf-ci-action@v1
  with:
    esp_idf_version: v5.4.4
    target: esp32c3
    path: ./
    command: >
      bash -c "idf.py build &&
      esptool.py --chip esp32c3 merge_bin
      -o build/<project>_merged.bin
      --flash_mode dio --flash_freq 80m --flash_size 4MB
      0x0 build/bootloader/bootloader.bin
      0x8000 build/partition_table/partition-table.bin
      0x10000 build/<project>.bin"
```

### Common CI Pitfalls

| Problem | Solution |
|---------|----------|
| `Include directory not found` | Use `${CMAKE_SOURCE_DIR}/main/` for paths outside component |
| `esp_timer.h: No such file` | Remove unused includes; add `esp_timer` to REQUIRES if needed |
| Wrong bin filename | Match `project()` name in top-level CMakeLists.txt |
| Docker path issue | All build ops must be in `command` parameter |

## Step 6: Adding a New Radar Type

1. Read and parse the new radar's PDF protocol documentation
2. Write `docs/<RADAR>_protocol.md`
3. Create `components/radar/src/<radar>/radar_<radar>.h` and `.c`
4. Add `config RADAR_<NAME>` in Kconfig
5. Add `#elif defined(CONFIG_RADAR_<NAME>)` in radar.h
6. Add source file condition in CMakeLists.txt
7. Commit, push, verify CI passes

## LD2460 Protocol Quick Reference

- **Default baud rate**: 115200
- **Report frame**: `F4 F3 F2 F1 | 04 | [len] | [X Y pairs] | F8 F7 F6 F5`
- **Command frame**: `FD FC FB FA | [func] | [len] | [data] | 04 03 02 01`
- **Target data**: X(int16) + Y(int16) per target, precision 0.1m
- **13 function codes**: 0x04(report), 0x06(ctrl), 0x07-0x08(install), 0x09-0x0A(mode), 0x0B(version), 0x0D(restart), 0x0E(baud), 0x10(reset), 0x11-0x12(range), 0x13-0x14(sensitivity)
