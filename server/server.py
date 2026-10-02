"""
Motion Detection and Face Identification - Server Side
--------------------------------------------------------
Receives JPEG frames from the ESP32-CAM via HTTP POST, runs face
detection + recognition against a folder of known faces, and returns
the identification result.

Setup:
    pip install flask face_recognition opencv-python numpy

Folder structure expected:
    known_faces/
        alice.jpg
        bob.jpg
        ...
    (filename without extension = the name used for identification)

Run:
    python server.py
Server listens on 0.0.0.0:5000 -- make sure your firewall allows
inbound connections on port 5000, and that this machine and the
ESP32-CAM are on the same Wi-Fi network.
"""

import os
import io
import datetime

import cv2
import numpy as np
import face_recognition
from flask import Flask, request, jsonify

app = Flask(__name__)

KNOWN_FACES_DIR = "known_faces"
CAPTURES_DIR = "captures"          # saved incoming frames, for your report/demo
MATCH_TOLERANCE = 0.6              # lower = stricter match, 0.6 is the common default

os.makedirs(KNOWN_FACES_DIR, exist_ok=True)
os.makedirs(CAPTURES_DIR, exist_ok=True)

known_encodings = []
known_names = []


def load_known_faces():
    """Load and encode every image in known_faces/ at startup."""
    known_encodings.clear()
    known_names.clear()

    for filename in os.listdir(KNOWN_FACES_DIR):
        if not filename.lower().endswith((".jpg", ".jpeg", ".png")):
            continue
        path = os.path.join(KNOWN_FACES_DIR, filename)
        image = face_recognition.load_image_file(path)
        encodings = face_recognition.face_encodings(image)

        if len(encodings) == 0:
            print(f"[WARN] No face found in {filename}, skipping.")
            continue

        known_encodings.append(encodings[0])
        known_names.append(os.path.splitext(filename)[0])

    print(f"[INFO] Loaded {len(known_names)} known face(s): {known_names}")


@app.route("/upload", methods=["POST"])
def upload():
    """Receives a raw JPEG frame from the ESP32-CAM."""
    image_bytes = request.get_data()

    if not image_bytes:
        return jsonify({"status": "error", "message": "No image data received"}), 400

    # Save the incoming frame (useful for your report screenshots / debugging)
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    save_path = os.path.join(CAPTURES_DIR, f"capture_{timestamp}.jpg")
    with open(save_path, "wb") as f:
        f.write(image_bytes)

    # Decode JPEG bytes into an OpenCV/numpy image
    np_arr = np.frombuffer(image_bytes, np.uint8)
    frame = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)

    if frame is None:
        return jsonify({"status": "error", "message": "Could not decode image"}), 400

    # face_recognition expects RGB, OpenCV gives BGR
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

    face_locations = face_recognition.face_locations(rgb_frame)
    face_encodings = face_recognition.face_encodings(rgb_frame, face_locations)

    if len(face_encodings) == 0:
        print(f"[{timestamp}] No face detected in frame.")
        return jsonify({"status": "no_face", "message": "No face detected"}), 200

    results = []
    for encoding in face_encodings:
        matches = face_recognition.compare_faces(known_encodings, encoding, tolerance=MATCH_TOLERANCE)
        distances = face_recognition.face_distance(known_encodings, encoding)

        name = "Unknown"
        if len(distances) > 0:
            best_match_index = np.argmin(distances)
            if matches[best_match_index]:
                name = known_names[best_match_index]

        results.append(name)
        print(f"[{timestamp}] Face identified as: {name}")

    return jsonify({"status": "ok", "faces_found": len(results), "identities": results}), 200


@app.route("/", methods=["GET"])
def health_check():
    return jsonify({"status": "running", "known_faces": known_names})


if __name__ == "__main__":
    load_known_faces()
    app.run(host="0.0.0.0", port=5000, threaded=True)
