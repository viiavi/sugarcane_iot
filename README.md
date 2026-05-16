<div align="center">
  <h1> Sugarcane IoT: Decentralized Irrigation Consensus</h1>
  <p>
    <strong>A Robust, Peer-to-Peer IoT Irrigation System Built on Heterogeneous Hardware (STM32 & ESP8266)</strong>
  </p>
  <p>
    <a href="https://github.com/viiavi/sugarcane_iot/commits/main"><img src="https://img.shields.io/github/last-commit/viiavi/sugarcane_iot?style=flat-square&color=blue" alt="Last Commit"></a>
    <a href="https://github.com/viiavi/sugarcane_iot/issues"><img src="https://img.shields.io/github/issues/viiavi/sugarcane_iot?style=flat-square&color=orange" alt="Issues"></a>
    <a href="https://github.com/viiavi/sugarcane_iot/stargazers"><img src="https://img.shields.io/github/stars/viiavi/sugarcane_iot?style=flat-square&color=gold" alt="Stars"></a>
    <a href="https://github.com/viiavi/sugarcane_iot/network/members"><img src="https://img.shields.io/github/forks/viiavi/sugarcane_iot?style=flat-square&color=lightgray" alt="Forks"></a>
  </p>
</div>

---

##  Overview

**Sugarcane IoT** is an advanced, decentralized irrigation system built to operate reliably in agricultural environments without relying on centralized cloud-control loops. By leveraging peer-to-peer consensus across edge nodes, the system ensures consistent and fault-tolerant irrigation logic.

This repository contains the complete firmware and documentation for the entire hardware stack, encompassing **STM32 microcontrollers** for low-level sensor integration/actuation and **NodeMCU (ESP8266/ESP32)** for lightweight telemetry and cloud bridging.

###  Key Features

- **Decentralized Decision Making:** STM32 nodes share state and execute a unified state machine for irrigation, ensuring reliability even if the main gateway drops.
- **Heterogeneous Hardware Mesh:** Seamless interoperability between high-performance ARM Cortex-M4/M4F processors (STM32F401RE/STM32F303RE) and low-cost Wi-Fi SoCs (NodeMCU).
- **Fault-Tolerant Fallbacks:** Built-in safeguards to gracefully handle sensor faults, link drops, or node failures.
- **Real-time Telemetry Pipeline:** Aggregated telemetry parsing (Temperature, Humidity, Soil/Water metrics) formatted for immediate Grafana/Prometheus ingestion via a Python bridge.

---

##  Repository Structure

The codebase is organized by hardware node and functional domain:

```text
sugarcane_iot/
├── 303re/                  # Firmware for STM32F303RE Node (Sensor & Actuator Logic)
├── 401re_final/            # Firmware for STM32F401RE Node (Core processing & control)
├── NodeMCU/                # ESP8266/ESP32 Firmware (UART bridge & Telemetry forwarder)
├── docs/                   # Project documentation
│   └── architecture/       # System architecture, consensus design, and diagrams
├── results/                # Output telemetry, evaluation metrics, and test logs
└── README.md               # You are here!
```

> **Note:** Additional developmental folders (e.g., `sugarcaneiot`, `STM32F303_Node`) are retained for historical reference and subsystem testing.

---

##  Hardware Stack

| Node Type | Microcontroller | Primary Responsibilities |
| :--- | :--- | :--- |
| **Node A (Edge)** | `STM32F303RE` | Reads DHT11/Soil Sensors, local edge compute, shares state via UART mesh. |
| **Node B (Core)** | `STM32F401RE` | Aggregates local metrics, validates consensus constraints, triggers water relays. |
| **Gateway** | `NodeMCU` | Serial monitor on D7 (`GPIO13`), formats JSON telemetry, bridges offline mesh to the cloud. |

---

##  Getting Started

### Prerequisites

- **STM32CubeIDE**: For compiling and flashing the `303re` and `401re_final` projects.
- **PlatformIO / Arduino IDE**: For compiling the `NodeMCU` bridge firmware.
- **ST-Link v2**: For flashing the Nucleo boards.

### Setup Instructions

1. **Clone the Repository:**
   ```bash
   git clone https://github.com/viiavi/sugarcane_iot.git
   cd sugarcane_iot
   ```
2. **Flash the STM32 Nodes:**
   - Open `STM32CubeIDE`.
   - Import the `303re` and `401re_final` projects from the root directory.
   - Build and run to flash the respective Nucleo-64 boards.
3. **Flash the NodeMCU:**
   - Open `NodeMCU/sugarcane_node.ino` in Arduino IDE or PlatformIO.
   - Connect the NodeMCU via USB and flash.
4. **Wiring the Mesh:**
   - Connect `STM32 TX` to `NodeMCU D7 (GPIO13)`.
   - Ensure common Ground (`GND`) across all microcontrollers.
   - Power up the system; the NodeMCU will begin broadcasting formatted telemetry at `115200` baud.

---

##  Results & Telemetry

Test logs, performance evaluations, and graphical representations of the consensus protocol in action can be found in the [`results/`](./results) directory. We actively monitor:
- Packet drop rates across the UART mesh.
- State-convergence latency between the STM32 nodes.
- Environmental data trends (Temperature, Humidity, Moisture).

---

##  Architecture

Curious about how the decentralized consensus works under the hood? 
Check out the detailed **[System Architecture Documentation](./docs/architecture/system_architecture.md)**.

---

<div align="center">
  <i>Built with ❤️ for precision agriculture.</i>
</div>
