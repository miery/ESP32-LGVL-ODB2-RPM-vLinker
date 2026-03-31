# 🏎️ ESP32-S3 Bluetooth OBD-II Dashboard

A real-time automotive data visualizer designed for the **VIEWE-SMARTRING** (ESP32-S3 AMOLED) and **vLinker MC-IOS** (ELM327) adapters.

---

## 📱 Hardware Platform: VIEWE-SMARTRING
This project is built specifically for the **VIEWE-SMARTRING**, a high-end circular display module. 

* **MCU:** ESP32-S3 with PSRAM
* **Display:** 1.43" AMOLED Circular Panel (SH8601)
* **Resolution:** 466x466 px
* **Manufacturer Source:** [Official VIEWE-SMARTRING GitHub](https://github.com/VIEWESMART/VIEWE-SMARTRING/tree/main)

---

## 🛠️ Features
* **Bluetooth Low Energy (NimBLE):** Fast scanning and stable connection to the `vLinker MC-IOS` adapter.
* **LVGL 8.3 Graphics:** Smooth UI rendering with high-resolution fonts and flicker-free DMA buffering.
* **OBD-II Protocol Parsing:** Real-time extraction of Engine RPM (PID `01 0C`).
* **Auto-Reconnect:** Automatic system recovery and re-scanning if the BLE connection is interrupted.
* **Optimized Performance:** Uses QSPI interface for the AMOLED panel to achieve high frame rates.

---

## 🚀 Getting Started

### Prerequisites
* **ESP-IDF v5.x** (Developed and tested on v5.5.3).
* **vLinker MC-IOS** or any ELM327-compatible BLE 4.0+ adapter.
* **VIEWE-SMARTRING** hardware.

### Installation
1. Clone the repository:
   ```bash
   git clone [https://github.com/miery/ESP32-LGVL-ODB2-RPM-vLinker]([https://github.com/YOUR_USERNAME/YOUR_REPO_NAME.git](https://github.com/miery/ESP32-LGVL-ODB2-RPM-vLinker))
   cd ESP32-LGVL-ODB2-RPM-vLinker
