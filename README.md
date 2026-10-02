# Motion Detection and Face Identification using ESP32-CAM

An IoT-based smart surveillance system that detects motion using a PIR sensor, captures an image via ESP32-CAM, and identifies the person using a Python face recognition backend.

## How it works

1. A **PIR sensor** detects motion and signals the ESP32-CAM
2. The **ESP32-CAM** captures a JPEG frame and sends it over Wi-Fi (HTTP POST) to a Python server
3. The **Python server** (Flask + OpenCV + face_recognition) detects and identifies the face against a set of known faces
4. The ESP32-CAM also hosts a **live local webpage** showing motion status and the captured photo, with LED/buzzer feedback

## Hardware

- ESP32-CAM (AI-Thinker, OV2640)
- PIR motion sensor (HC-SR501)
- 2x LEDs (green = idle, red = motion detected) + buzzer
- FTDI USB-to-TTL programmer (for flashing)
- External 5V power supply

## Software / Libraries

**Firmware (Arduino/C++):**
- `esp_camera.h` — camera driver
- `WiFi.h` — network connection, static IP
- `WebServer.h` — hosts the live status webpage
- `HTTPClient.h` — sends captured images to the server

**Server (Python):**
- Flask — HTTP server
- OpenCV — image decoding
- dlib + face_recognition — face detection and identification
- NumPy — array handling

See [`docs/SOFTWARE_STACK.md`](docs/SOFTWARE_STACK.md) for a full breakdown of every dependency and why it's used.

## Repository structure

```
firmware/     ESP32-CAM Arduino sketch (final firmware)
server/       Python face recognition server + known_faces folder structure
docs/         Circuit schematic, software documentation, full project report (PDF)
```

## Setup

### 1. Flash the firmware
Open `firmware/ESP32CAM_FINAL_FIRMWARE.ino` in Arduino IDE, update your WiFi credentials and static IP to match your network, then flash via FTDI (AI-Thinker ESP32-CAM board setting).

### 2. Set up the Python server
```bash
cd server
python3 -m venv face_env
source face_env/bin/activate
pip install flask opencv-python numpy dlib face_recognition
```

Add reference photos to `server/known_faces/` (filename = person's name, e.g. `john.jpg`).

### 3. Run the server
```bash
python3 server.py
```

### 4. Power on the ESP32-CAM
Once connected to WiFi, visit its static IP in a browser to see the live status page, and trigger motion to test the full pipeline.

## Circuit Diagram

See [`docs/circuit_schematic.png`](docs/circuit_schematic.png) for the full wiring diagram.

## Project Report

Full college mini-project report available at [`docs/Mini_Project_Report.pdf`](docs/Mini_Project_Report.pdf).

## Notes

- GPIO 16/17 are reserved for PSRAM on the ESP32-CAM — avoid using them for peripherals
- The PIR sensor's trigger mode jumper should be set to **H (repeatable trigger)** for reliable detection
- `known_faces/` is intentionally empty in this repository for privacy — add your own reference photos locally

## License

MIT (or your preferred license — update as needed)
