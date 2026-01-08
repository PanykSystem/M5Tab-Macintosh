# Hello World - M5Stack Tab5

A minimal Hello World application for the M5Stack Tab5, demonstrating WiFi connectivity and a web-based device stats dashboard.

## Features

- **Hello World Display** - Attractive centered text on the 1280x720 display
- **WiFi Connection** - Connects to configured WiFi with visual progress feedback
- **Device Stats Web Server** - View CPU, RAM, and Flash usage via web browser

## Hardware

### M5Stack Tab5

The Tab5 features a dual-chip architecture:

| Chip | Role | Key Features |
|------|------|--------------|
| **ESP32-P4** | Main Application Processor | 400MHz dual-core RISC-V, 32MB PSRAM |
| **ESP32-C6** | Wireless Co-processor | WiFi 6, Bluetooth LE 5.0 |

See [boardConfig.md](boardConfig.md) for detailed hardware specifications and pin mappings.

## Building

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE extension)
- USB-C cable for programming

### Build & Upload

```bash
# Build the project
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor
```

## Project Structure

```
├── src/
│   ├── main.cpp           # Application entry point
│   ├── config.h           # WiFi credentials and configuration
│   ├── web_server.*       # HTTP server serving device stats
│   └── ui/
│       └── ui_init.*      # Display initialization
├── scripts/               # Build scripts
└── platformio.ini         # PlatformIO configuration
```

## Web Dashboard

Once connected to WiFi, access the device stats at the displayed IP address. The dashboard shows:

- Chip model and CPU frequency
- Heap memory usage
- PSRAM usage
- Flash storage usage
- WiFi signal strength
- Device uptime

The page auto-refreshes every 5 seconds.

## Configuration

Edit `src/config.h` to customize:

- `WIFI_SSID` - WiFi network name
- `WIFI_PASSWORD` - WiFi password
- `WIFI_CONNECT_TIMEOUT_MS` - Connection timeout

## License

MIT License
