#!/usr/bin/env python3
"""
CamCanvas UDP Commander
-----------------------
A simple utility to send UDP commands to the CamCanvas unit of the Emily robot.
Uncomment the command you want to send and run the script. Great for debugging or just for fun.

Usage:
    python camcanvas_commander.py

Configuration:
    - CAMCANVAS_IP: The static IP address of your CamCanvas unit
    - CAMCANVAS_PORT: The UDP port CamCanvas listens on
    - LOCAL_PORT: The port this script listens on for responses
"""

import socket
import json
import time

# --- Configuration ---
CAMCANVAS_IP = "192.168.68.203"
CAMCANVAS_PORT = 12347
LOCAL_PORT = 12345  # Port to receive responses on
RESPONSE_TIMEOUT = 30  # Seconds to wait for a response

# --- Image Generation Models ---
# Available models for the generate_image command:
#   "venice-sd35"          - Stable Diffusion 3.5 (default)
#   "wai-illustrious"      - Wai Illustrious (uncensored anime)
# check out all currently available models in the venice.ai api documentation.

def send_command(command_dict, wait_for_response=True):
    """Send a JSON command to CamCanvas via UDP and optionally wait for response."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", LOCAL_PORT))
    sock.settimeout(RESPONSE_TIMEOUT)

    message = json.dumps(command_dict)
    print(f"Sending to {CAMCANVAS_IP}:{CAMCANVAS_PORT}")
    print(f"Command: {message}")

    sock.sendto(message.encode(), (CAMCANVAS_IP, CAMCANVAS_PORT))
    print("Sent.")

    if wait_for_response:
        print(f"Waiting for response (timeout: {RESPONSE_TIMEOUT}s)...")
        try:
            data, addr = sock.recvfrom(4096)
            response = data.decode()
            print(f"Response from {addr}:")
            try:
                parsed = json.loads(response)
                print(json.dumps(parsed, indent=2))
            except json.JSONDecodeError:
                print(response)
        except socket.timeout:
            print("No response received (timeout).")

    sock.close()
    print("---")


# ============================================================
# COMMANDS — Uncomment ONE command block below and run the script
# ============================================================

# --- Move Head ---
# Pan: horizontal rotation (angle in degrees)
# Tilt: vertical rotation (angle in degrees, clamped by CamCanvas)
send_command({
    "command": "move_head",
    "pan": 90,
    "tilt": 90
}, wait_for_response=False)

# --- Nod Head ---
# A quick nod to the specified angle and back
# send_command({
#     "command": "nod_head",
#     "angle": 105
# }, wait_for_response=False)

# --- Generate Image ---
# Generates an image from a text prompt and displays it on the 3.5" TFT
# send_command({
#     "command": "generate_image",
#     "prompt": "a mysterious robot looking at a sunset, digital art",
#     "model": "wai-illustrious"
# })

# --- Analyze Vision ---
# Takes a photo and sends it to Venice Vision API with the given prompt
# use_flash: 1 = LED flash on during capture, 0 = no flash
# send_command({
#     "command": "analyze_vision",
#     "prompt": "Describe what you see in detail.",
#     "use_flash": 0
# })

# --- Take Picture ---
# Captures a photo and saves it to the SD card
# send_command({
#     "command": "take_picture"
# })

# --- Set Status LED ---
# Control the onboard NeoPixel LED
# Effects: "none", "pulse", "blink"
# Speed: milliseconds per cycle
# send_command({
#     "command": "set_status_led",
#     "r": 0,
#     "g": 50,
#     "b": 255,
#     "effect": "pulse",
#     "speed": 1000
# }, wait_for_response=False)

# --- Turn LED Off ---
# send_command({
#     "command": "set_status_led",
#     "r": 0,
#     "g": 0,
#     "b": 0,
#     "effect": "none"
# }, wait_for_response=False)
