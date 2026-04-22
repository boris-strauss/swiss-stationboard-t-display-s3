# Swiss Station Board Display

A real-time public transport departure board for the **LilyGO T-Display S3**, showing live tram and train schedules from any Swiss station. Data is fetched from the [Swiss public transport API](https://transport.opendata.ch/).

## Features

- **Live departures** — fetches real-time data from transport.opendata.ch every 20 seconds
- **Two display modes:**
  - **Tram** — sorted by actual arrival time, shows minutes remaining or a bus icon
  - **Train** — sorted by scheduled time, shows HH:MM and delay column
- **Color-coded line numbers** — uses official Swiss transit line colors (ZVV, SBB, etc.)
- **Custom pixel font** — renders destination names with proper Swiss character support (ä, ö, ü, à, è, é)
- **WiFi captive portal** — configure station, WiFi, and display mode from any phone/browser
- **Station autocomplete** — full list of Swiss stations from SBB OpenData, searchable with fuzzy Umlaut matching
- **Page toggle** — press the side button to view more departures; auto-reverts after 10 seconds
- **Setup mode** — hold the boot button for 3 seconds to reopen the configuration portal

## Hardware

- [LilyGO T-Display S3](https://www.lilygo.cc/products/t-display-s3) (ESP32-S3, 320×170 ST7789 TFT)

## Getting Started

### Prerequisites

- [VS Code](https://code.visualstudio.com/) with [PlatformIO](https://platformio.org/install/ide?install=vscode)
- Python 3 (used by pre-build scripts)

### Build & Flash

1. Clone this repository
2. Open the folder in VS Code with PlatformIO
3. Click **Build** (✔) — the pre-build scripts will automatically:
   - Download the latest Swiss station list from SBB OpenData
   - Fetch official transit line colors from GitHub
   - Generate the custom pixel font header
4. Connect the T-Display S3 via USB
5. Click **Upload** (→)

### First-Time Setup

On first boot (or when no WiFi is saved), the device opens a WiFi access point called **VBZ-Display-Setup**. Connect to it from your phone and configure:

- **WiFi network** and password
- **Station name** (e.g. "Zürich, Helmhaus" or "Bern")
- **Display mode** (Tram or Train)

Settings are stored in flash and persist across reboots. Hold the **boot button** for 3 seconds to re-enter setup.

## Project Structure

```
├── src/
│   ├── main.cpp              # Application entry point & button logic
│   ├── DisplayManager.cpp/h  # TFT rendering, custom font engine, color lookup
│   ├── NetworkManager.cpp/h  # WiFi, captive portal, settings storage
│   ├── TransportAPI.cpp/h    # API client for transport.opendata.ch
│   ├── config.h              # Refresh interval and other constants
│   ├── pin_config.h          # T-Display S3 GPIO pin definitions
│   └── bitmaps_vbz_font_wgemini.txt  # Source bitmaps for custom font
├── lib/
│   └── TFT_eSPI/             # TFT_eSPI with T-Display S3 board setup
├── boards/
│   └── lilygo-t-displays3.json  # PlatformIO board definition
├── generate_stations.py      # Pre-build: downloads Swiss station list → stations.h
├── fetch_colors.py           # Pre-build: downloads transit line colors → line_colors_data.h
├── generate_font.py          # Pre-build: converts font bitmaps → vbz_font.h
└── platformio.ini            # Build configuration
```

## API

This project uses the free [Swiss public transport API](https://transport.opendata.ch/) — no API key required.

## Acknowledgments

This project includes a modified copy of [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (Bodmer, custom board setup for T-Display S3). Hardware board definition originates from the [LilyGO T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3) repository.
