# Vertical Garden AI 🌿🤖

> Smart IoT system for vertical garden monitoring & automation

## Architecture

```
💻 Chaba (Coding) → git push → GitHub
                                 ↓
                    GitHub Actions (Auto Compile)
                                 ↓
                         Release (.bin)
                                 ↓
        🖥️ Laila (Pi4) → Download → OTA → 🔦 ESP32
```

## Components

- **ESP32 Firmware** (`firmware/`) — Sensor reading + MQTT + OTA
- **GitHub Actions** (`.github/workflows/`) — Auto compile & release
- **Docs** (`docs/`) — Wiring, calibration, setup guides

## Workflow

1. Chaba pushes code → GitHub Actions compiles firmware
2. Release created with `.bin` artifact
3. Laila detects new release → OTA updates ESP32
4. ESP32 sensors → MQTT → Laila (Node-RED Dashboard)

## Sensors

- BH1750 (x4) — Light intensity
- AM2315 — Temperature & Humidity
- *(More to come...)*

## License

Private — © Aroon Wiriyokhoon
