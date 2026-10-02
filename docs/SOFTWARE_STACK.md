# Software Stack Documentation
## Motion Detection and Face Identification using ESP32-CAM

This document explains every library and dependency used in the project — both on the **ESP32-CAM firmware side** (C++/Arduino) and the **Python face recognition server side** — what each one does, and why it was chosen.

---

## 1. Firmware Side (ESP32-CAM, C++/Arduino)

### `esp_camera.h`
**What it is:** Espressif's official low-level driver for the OV2640 camera sensor used on the ESP32-CAM module.

**Why it's used:** This library handles direct communication with the camera hardware over the DVP parallel interface (data lines, clock, sync signals) and the SCCB control bus. Without it, there is no way to configure the camera sensor (resolution, JPEG quality, frame buffering) or actually pull an image frame off the hardware.

**Key functions used in this project:**
- `esp_camera_init()` — configures and starts the camera with our chosen pin mapping, resolution, and buffer settings
- `esp_camera_fb_get()` — captures a frame and returns a pointer to the JPEG data
- `esp_camera_fb_return()` — releases the frame buffer back to the driver once we're done reading it, so it can be reused for the next capture

---

### `WiFi.h`
**What it is:** The ESP32 core's built-in WiFi management library.

**Why it's used:** Handles joining the WiFi network (Station mode, `WiFi.begin()`), assigning a fixed IP address (`WiFi.config()`), and reporting connection status. This is what lets the ESP32-CAM communicate with the Python server and serve its own webpage over the local network.

**Key functions used:**
- `WiFi.mode(WIFI_STA)` — sets the radio to Station mode (join an existing network, rather than hosting one)
- `WiFi.config()` — applies our static IP, gateway, and subnet before connecting
- `WiFi.setSleep(false)` — disables WiFi power-saving, which otherwise adds latency to every request
- `WiFi.begin()` / `WiFi.status()` — initiates and monitors the connection

---

### `WebServer.h`
**What it is:** A lightweight HTTP server library built for ESP32 boards.

**Why it's used:** This is what allows the ESP32-CAM to host its own webpage (`http://10.203.29.184`) showing live motion status and the last captured photo — without needing any external web server software. It listens for incoming HTTP requests and routes them to handler functions.

**Key functions used:**
- `server.on("/", handleRoot)` — serves the HTML status page
- `server.on("/status", handleStatus)` — serves a small JSON object the page polls every 500ms
- `server.on("/image", handleImage)` — serves the most recently captured JPEG
- `server.handleClient()` — must be called continuously in `loop()` to process incoming requests

---

### `HTTPClient.h`
**What it is:** A library for making outgoing HTTP requests **from** the ESP32 (as opposed to `WebServer.h`, which handles incoming requests).

**Why it's used:** This is how the ESP32-CAM pushes each captured image to the Python server running on the Kali VM. When motion is detected, the firmware uses `HTTPClient` to POST the raw JPEG bytes to `http://10.203.29.40:5000/upload`.

**Key functions used:**
- `http.begin(serverUrl)` — targets the request at the server's address
- `http.addHeader("Content-Type", "image/jpeg")` — tells the server what kind of data is coming
- `http.POST(buffer, length)` — sends the actual image bytes
- `http.getString()` — reads back the server's JSON response (e.g., identified name)

---

## 2. Server Side (Python, running on Kali Linux)

### Flask
**What it is:** A lightweight Python web framework.

**Why it's used:** Flask provides the HTTP server that receives images POSTed by the ESP32-CAM. It's chosen over heavier frameworks (like Django) because this project only needs a couple of simple routes (`/upload`, `/`) with no database, templating, or user authentication — Flask's minimal footprint keeps the server fast and easy to understand.

**Key parts used:**
- `Flask(__name__)` — creates the application instance
- `@app.route("/upload", methods=["POST"])` — defines the endpoint that receives images
- `request.get_data()` — reads the raw bytes sent by the ESP32-CAM
- `jsonify()` — formats the identification result as JSON to send back
- `app.run(host="0.0.0.0", port=5000)` — starts the server, listening on all network interfaces so the ESP32-CAM (on the same LAN) can reach it

---

### OpenCV (`opencv-python`, imported as `cv2`)
**What it is:** A widely used open-source computer vision library.

**Why it's used:** OpenCV is used here purely for **image decoding and color conversion**, not for the face recognition itself:
- `cv2.imdecode()` — converts the raw JPEG bytes received over HTTP into a usable image matrix (the same kind of array a webcam feed would produce)
- `cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)` — OpenCV loads images in BGR channel order by convention, but the `face_recognition` library expects RGB order, so this conversion is a required bridge between the two libraries

---

### NumPy
**What it is:** The core numerical computing library for Python, providing fast array operations.

**Why it's used:** Both OpenCV and `face_recognition` represent images and face encodings as NumPy arrays under the hood. In this project specifically:
- `np.frombuffer()` — converts the raw incoming byte stream into a NumPy array that OpenCV can decode into an image
- `np.argmin(distances)` — finds the closest-matching known face by picking the smallest distance value in the comparison results

---

### dlib
**What it is:** A C++ toolkit with Python bindings, containing machine learning and image processing algorithms — most relevantly, a pretrained face detection and face landmark model.

**Why it's used:** `dlib` is the actual engine underneath `face_recognition` — it provides:
- A **HOG (Histogram of Oriented Gradients)** based face detector, which locates where faces are in an image
- A **pretrained deep metric learning model** that converts a detected face into a 128-dimensional numerical encoding (a compact numerical "fingerprint" of facial features)

**Why it needed special installation steps:** `dlib` is written in C++ and needs to be compiled for your specific system — this is why installing it on Windows required Visual Studio Build Tools or a prebuilt wheel, while on Kali Linux it compiled more smoothly using `cmake` and `build-essential`, both of which provide the compiler toolchain `dlib`'s setup script calls during installation.

---

### face_recognition
**What it is:** A high-level Python wrapper around `dlib`'s face detection and encoding models, built by Adam Geitgey, designed to make face recognition accessible without needing to work with `dlib`'s lower-level C++ API directly.

**Why it's used:** This is the library doing the actual "is this a known person" logic in `server.py`:
- `face_recognition.face_locations(image)` — returns bounding boxes of any faces found in the frame (using dlib's HOG detector internally)
- `face_recognition.face_encodings(image, locations)` — converts each detected face into its 128-number encoding (using dlib's deep learning model internally)
- `face_recognition.load_image_file(path)` — loads a reference photo from `known_faces/` for encoding at startup
- `face_recognition.compare_faces(known_encodings, encoding, tolerance=0.6)` — checks whether a new face's encoding is close enough to any known encoding to count as a match
- `face_recognition.face_distance(known_encodings, encoding)` — calculates the actual numerical distance between faces, used to pick the *closest* match rather than just any match under the threshold

**Why tolerance = 0.6:** This is the commonly recommended default in the library's own documentation — a lower value (e.g., 0.4) makes matching stricter (fewer false positives, but might reject genuine matches in poor lighting), while a higher value (e.g., 0.8) is more lenient but risks matching the wrong person. 0.6 is a balanced middle ground suitable for a general-purpose demo like this project.

---

## 3. How These Libraries Work Together (Data Flow)

```
ESP32-CAM firmware                          Python server (server.py)
─────────────────────                       ──────────────────────────
esp_camera.h                                 Flask
   captures JPEG frame                          receives POSTed JPEG bytes
        |                                            |
        v                                            v
HTTPClient.h                                 OpenCV (cv2)
   POSTs JPEG bytes over WiFi  ------------>   decodes JPEG -> image array
        |                                       converts BGR -> RGB
        |                                            |
        v                                            v
WiFi.h                                       face_recognition (+ dlib)
   maintains network connection                 detects face location(s)
        |                                       generates 128-d encoding
        v                                       compares to known_faces/
WebServer.h                                          |
   hosts local status page                           v
   shows result to user                        NumPy
                                                   finds closest match
                                                      |
                                                      v
                                                Flask
                                                   returns JSON result
                                                   ("known" name / "unknown")
```

---

## 4. Summary Table

| Library | Language/Side | Role |
|---|---|---|
| `esp_camera` | C++ (ESP32) | Captures JPEG frames from the OV2640 sensor |
| `WiFi` | C++ (ESP32) | Connects to the network, manages static IP |
| `WebServer` | C++ (ESP32) | Hosts the ESP32's own live status webpage |
| `HTTPClient` | C++ (ESP32) | Sends captured images to the Python server |
| Flask | Python | Runs the server, receives images, returns results |
| OpenCV (`cv2`) | Python | Decodes JPEG bytes, converts color format |
| NumPy | Python | Array handling for images and face encodings |
| dlib | Python (C++ backend) | Underlying face detection + encoding models |
| face_recognition | Python | High-level face detection/recognition API built on dlib |

---

*This document is intended to accompany the main project report, providing a technical reference for the software architecture of the Motion Detection and Face Identification system.*
