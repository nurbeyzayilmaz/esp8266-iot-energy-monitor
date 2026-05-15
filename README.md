<div align="center">

# ⚡ ESP8266 IoT Energy Monitor

**Real-time energy monitoring & control system built from scratch**

[![Live Demo](https://img.shields.io/badge/Live_Demo-Online-00C7B7?style=for-the-badge&logo=netlify&logoColor=white)](https://enerjitakipsistemi.netlify.app)
[![Platform](https://img.shields.io/badge/Platform-ESP8266-E7352C?style=for-the-badge&logo=espressif&logoColor=white)](https://github.com/nurbeyzayilmaz/esp8266-iot-energy-monitor)
[![Language](https://img.shields.io/badge/Firmware-C%2B%2B-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](firmware/)
[![Web](https://img.shields.io/badge/Web-Next.js_15-000000?style=for-the-badge&logo=nextdotjs&logoColor=white)](web-dashboard/)
[![Database](https://img.shields.io/badge/Database-Supabase-3ECF8E?style=for-the-badge&logo=supabase&logoColor=white)](https://supabase.com)

*A personal IoT project that measures, monitors, and controls household electrical consumption in real-time.*

</div>

---

## 📖 Overview

This project is a complete, end-to-end IoT energy monitoring system that I designed and built entirely from scratch. It reads electrical measurements from a **PZEM-004T v3.0** power sensor via **Modbus RTU** protocol, streams data to **Supabase** (PostgreSQL), and displays it on a **Next.js** real-time web dashboard. The system also allows remote relay control to cut power automatically when consumption exceeds a threshold.

> **Status:** Actively running 24/7 on a physical hardware prototype in a custom wooden enclosure.

---

## 🔄 Two Firmware Versions

This project went through two development phases, both of which are preserved in this repository:

| | [Blynk Version](firmware/blynk/sketch_blynk.ino) | [Supabase Version](firmware/enerji-takip-esp8266.ino) |
|---|---|---|
| **Backend** | Blynk IoT Cloud | Supabase (PostgreSQL) |
| **Dashboard** | Blynk mobile + web app | Custom Next.js web app |
| **Protocol** | Blynk virtual pins (V2–V16) | HTTPS REST API |
| **Data storage** | Blynk cloud (ephemeral) | PostgreSQL (persistent) |
| **Realtime** | Blynk WebSocket | Supabase Realtime WebSocket |
| **Custom logic** | Blynk timer-based | Polling + command table pattern |
| **Status** | ✅ Working prototype | ✅ Production (live demo) |

> The Blynk version was developed first as a rapid prototype. The Supabase version is a full rewrite that replaces the proprietary IoT platform with an open-source stack, enabling persistent data storage, custom dashboards, and full backend control.

---

## 🔍 Real-World Discovery — Phantom Load Detection

> *This capability was validated experimentally with a university faculty advisor.*

One of the most impactful findings during testing: **the system accurately detects phantom load (standby power draw)** — electricity consumed by devices that appear to be "off" but remain plugged into the grid.

### What is Phantom Load?

When an appliance is in standby mode, it continues drawing power from the grid even though it performs no useful work. This energy is silently added to your electricity bill every month.

### Experimental Result

| Device | State | Measured Power |
|--------|-------|---------------|
| Printer | **Standby** (display off, "sleeping") | **18 W** |
| Printer | Fully unplugged | 0 W |

A printer left plugged in overnight (8h) consumes **≈ 144 Wh = 0.144 kWh** of phantom energy — doing nothing.  
At ₺3.20/kWh → **≈ ₺0.46/night**, **≈ ₺168/year** wasted on a single device in standby.

### How the System Measures This

The PZEM-004T sensor computes **true active power** using the AC power formula:

```
P (W) = V_rms × I_rms × cos(φ)
```

Where:
- `V_rms` — Root Mean Square voltage (grid voltage)
- `I_rms` — Root Mean Square current (true current draw, including standby)
- `cos(φ)` — Power factor (ratio of real to apparent power)

This means the sensor captures **even the tiny, non-sinusoidal current drawn by switching power supplies in standby** — something a basic clamp meter often misses. The result is visible in real-time on the dashboard the moment a device transitions between states (off → standby → active → off).

### Why This Matters

- 🏭 **Industrial applications:** Identify energy-wasting equipment in factories during off-hours
- 🏠 **Smart home:** Detect which appliances to put on smart plugs
- 📊 **Energy auditing:** Quantify real idle consumption vs. rated power
- 💡 **Awareness:** Users see the exact cost of leaving devices plugged in

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        220V AC LINE                             │
│   [Plug] ──────── [PZEM-004T Clamp] ──────── [Load Device]    │
│                         │                          ↑            │
│                    (Modbus RTU)               [Relay NC]        │
│                    UART/Serial                     │            │
└──────────────────────────────────────────────────────────────────┘
                           │                         │
                    ┌──────▼──────┐                  │
                    │  ESP8266    │──── D1 ──────────┘
                    │  NodeMCU   │     (Relay Control - Active LOW)
                    │            │
                    │  D5 → RX   │  SoftwareSerial
                    │  D6 → TX   │  @ 9600 baud
                    └──────┬──────┘
                           │ HTTPS / REST API
                           │ (every 2 seconds)
                    ┌──────▼──────────────────┐
                    │     Supabase            │
                    │  ┌─────────────────┐   │
                    │  │ readings table  │   │
                    │  │ commands table  │   │
                    │  │ settings table  │   │
                    │  │ alerts table    │   │
                    │  └─────────────────┘   │
                    └──────┬──────────────────┘
                           │ Realtime WebSocket
                           │ + REST polling
                    ┌──────▼──────────────────┐
                    │   Next.js Dashboard     │
                    │   (Netlify deployed)    │
                    │   Recharts + TailwindCSS│
                    └─────────────────────────┘
```

---

## 🔩 Hardware Components

| Component | Model | Purpose |
|-----------|-------|---------|
| **Microcontroller** | ESP8266 NodeMCU v1.0 | WiFi + firmware execution |
| **Power Sensor** | PZEM-004T v3.0 | Voltage / current / power / energy measurement |
| **Relay Module** | SRD-05VDC-SL-C (Active-LOW) | Remote circuit breaker control |
| **Enclosure** | Custom wooden box | Physical housing |
| **Power Supply** | 5V USB adapter | NodeMCU power |
| **Wiring** | 220V AC rated cables | Main circuit wiring |

---

## 📌 Pin Connections

| ESP8266 Pin | Connected To | Purpose |
|-------------|--------------|---------|
| `D5` | PZEM-004T TX | SoftwareSerial RX |
| `D6` | PZEM-004T RX | SoftwareSerial TX |
| `D1` | Relay IN | Relay control (Active-LOW) |
| `D2` | Button (GND) | Physical relay toggle |
| `3V3 / GND` | PZEM VCC/GND | Sensor power |

> ⚠️ **Relay wiring:** Output cable must be on the **NC (Normally Closed)** terminal so default state = current flowing.

---

## ✨ Features

### Firmware (C++ / Arduino)
- [x] **Modbus RTU** communication with PZEM-004T over SoftwareSerial
- [x] Reads: voltage, current, power (W), energy (kWh), power factor, frequency
- [x] **True AC active power** measurement: `P = V_rms × I_rms × cos(φ)`
- [x] **Phantom load detection** — captures standby draw invisible to basic meters
- [x] **Exponential moving average** filter (α=0.3) for stable readings
- [x] Power offset correction for cable/measurement losses
- [x] **Over-consumption alert** → auto-writes to Supabase `alerts` table
- [x] Remote relay control via `commands` table polling (2s interval)
- [x] Physical button debounce with `millis()` (no `delay()`)
- [x] Dynamic threshold & price settings fetched from Supabase (60s refresh)
- [x] WiFi auto-reconnect with restart fallback (5 retries)
- [x] Heap memory watchdog (restarts if < 12KB free)
- [x] 3-second boot safety delay before accepting relay commands

### Web Dashboard (Next.js + Supabase)
- [x] **Supabase Realtime** WebSocket for instant updates
- [x] Polling fallback with Page Visibility API support
- [x] Real-time power chart (Recharts) — 15min / 1h / 6h / 24h ranges
- [x] 8 metric cards: voltage, current, power, energy, PF, frequency, max power, estimated bill
- [x] Remote relay toggle with optimistic UI update
- [x] Over-threshold warning banner with pulse animation
- [x] Energy counter reset & WiFi reset commands
- [x] Smart advisory system (5 tiers: idle → efficient → normal → high → critical)
- [x] Data freshness indicator (online / stale / offline)
- [x] Configurable settings: threshold, unit price, refresh interval, password

---

## 📸 Screenshots

### 🔩 Physical Prototype

<p align="center">
  <img src="docs/prototype-overview.jpeg" width="48%" alt="Hardware Overview"/>
  &nbsp;
  <img src="docs/prototype-closeup.jpeg" width="48%" alt="Hardware Close-up"/>
</p>

<p align="center"><i>Custom wooden enclosure housing ESP8266 NodeMCU, PZEM-004T v3.0 sensor, and Active-LOW relay module with full 220V AC wiring</i></p>

---

### 📊 Web Dashboard

![Web Dashboard](docs/blynk-dashboard.png)

*Real-time readings: voltage (151V), power (2706W), energy chart, estimated bill (₺1,905), relay control, and advisory system*

---

### ⚙️ Datastreams Configuration

![Datastreams](docs/blynk-datastreams.png)

*14 virtual datastreams: current, voltage, power, energy (kWh), frequency, power factor, relay switch, LED, button, advisory message, estimated bill, WiFi RSSI, uptime*

---

## 🗂️ Repository Structure

```
esp8266-iot-energy-monitor/
│
├── firmware/
│   ├── enerji-takip-esp8266.ino   # v2 — Supabase + REST API (production)
│   └── blynk/
│       └── sketch_blynk.ino       # v1 — Blynk IoT platform (prototype)
│
├── web-dashboard/
│   ├── app/
│   │   ├── page.tsx               # Login page
│   │   ├── layout.tsx             # Root layout
│   │   ├── providers.tsx          # React context providers
│   │   └── dashboard/
│   │       └── page.tsx           # Main real-time dashboard
│   ├── lib/
│   │   ├── supabase.ts            # Supabase client + data fetchers
│   │   └── types.ts               # TypeScript type definitions
│   └── .env.example               # Environment variables template
│
└── docs/
    ├── prototype-overview.jpeg    # Hardware photo — full view
    ├── prototype-closeup.jpeg     # Hardware photo — close-up
    ├── blynk-dashboard.png        # Web dashboard screenshot
    └── blynk-datastreams.png      # Datastreams config screenshot
```

---

## 🚀 Getting Started

### Firmware Setup (Supabase version)

1. Install Arduino IDE + ESP8266 board support
2. Install libraries: `PZEM004Tv30`, `ArduinoJson`, `ESP8266WiFi`
3. Open `firmware/enerji-takip-esp8266.ino` and fill in credentials:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
   const char* SUPABASE_URL = "https://YOUR_PROJECT_ID.supabase.co";
   const char* SUPABASE_KEY = "YOUR_SUPABASE_ANON_KEY";
   ```
4. Flash to NodeMCU at 115200 baud

### Firmware Setup (Blynk version)

1. Install libraries: `PZEM004Tv30`, `BlynkSimpleEsp8266`
2. Open `firmware/blynk/sketch_blynk.ino` and fill in:
   ```cpp
   #define BLYNK_TEMPLATE_ID   "YOUR_BLYNK_TEMPLATE_ID"
   #define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN"
   char ssid[] = "YOUR_WIFI_SSID";
   char pass[] = "YOUR_WIFI_PASSWORD";
   ```
3. Flash to NodeMCU at 115200 baud

### Web Dashboard Setup

```bash
cd web-dashboard
npm install
cp .env.example .env.local
npm run dev
```

---

## 🛠️ Tech Stack

| Layer | Technology |
|-------|-----------|
| **Microcontroller** | ESP8266 NodeMCU / Arduino C++ |
| **Sensor Protocol** | Modbus RTU (PZEM004Tv30) |
| **Cloud Database** | Supabase (PostgreSQL + Realtime) |
| **Frontend** | Next.js 15 + React + TailwindCSS |
| **Charts** | Recharts |
| **Deployment** | Netlify |

---

## 👤 Author

**Beyza Nur Yılmaz** — EEE Student, İnönü Üniversitesi

[![LinkedIn](https://img.shields.io/badge/LinkedIn-0A66C2?style=flat&logo=linkedin&logoColor=white)](https://www.linkedin.com/in/nur-beyza-yilmaz/)
[![GitHub](https://img.shields.io/badge/GitHub-181717?style=flat&logo=github&logoColor=white)](https://github.com/nurbeyzayilmaz)
[![Live Demo](https://img.shields.io/badge/Live_Demo-00C7B7?style=flat&logo=netlify&logoColor=white)](https://enerjitakipsistemi.netlify.app)

---

<div align="center">
<i>Built with real hardware. Running 24/7. Measured in kilowatt-hours.</i>
</div>