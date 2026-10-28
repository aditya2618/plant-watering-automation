# Eyum Plant Care - Documentation Index

Welcome to the technical documentation library for the **Eyum Plant Care** project.

## Documents

| Document | Purpose & Highlights |
| :--- | :--- |
| **[System Architecture](file:///d:/PROJECT/IOT-Projects/plant-watering%20automation/documentation/SYSTEM_ARCHITECTURE.md)** | Complete hardware circuit schematics, pinout assignments, Data Flow Diagrams (Level 0 Context and Level 1 Processes), State Machine workflows, REST API contracts, and memory persistence schemas. |
| **[1-Time Genesis Prompt](file:///d:/PROJECT/IOT-Projects/plant-watering%20automation/documentation/ONE_TIME_PROMPT.md)** | Production-ready master system prompt designed for AI coding assistants to recreate or scale this entire IoT project from scratch in a single shot. |

---

## Quick Hardware Reference

- **Soil Sensor Analog (`AO`)**: `GPIO 35`
- **Soil Sensor Digital (`DO`)**: `GPIO 32` *(Optional)*
- **Pump Relay / MOSFET**: `GPIO 26` *(Active-HIGH)*
- **Reservoir Float Switch**: `GPIO 34` *(Optional, Active-LOW)*
- **Local Dashboard URL**: `http://eyum-plant.local`
