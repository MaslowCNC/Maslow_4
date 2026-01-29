#!/usr/bin/env python3
"""
Firmware Simulator for Maslow CNC FluidNC ESP3D Interface
This simulator allows testing the ESP3D web interface with different Maslow states.
"""

import asyncio
import json
import time
from flask import Flask, request, send_from_directory
import websockets
import sys
import threading

# Maslow states from MaslowEnums.h
MASLOW_STATES = {
    0: "UNKNOWN",
    1: "RETRACTING",
    2: "RETRACTED",
    3: "EXTENDING",
    4: "EXTENDED",
    5: "TAKING_SLACK",
    6: "CALIBRATION_IN_PROGRESS",
    7: "READY_TO_CUT",
    8: "RELEASE_TENSION",
    9: "CALIBRATION_COMPUTING"
}

# Current state (can be changed via command line or environment)
current_state = 0
if len(sys.argv) > 1:
    try:
        current_state = int(sys.argv[1])
        if current_state < 0 or current_state > 9:
            print(f"Invalid state {current_state}, must be 0-9")
            sys.exit(1)
    except ValueError:
        print(f"Invalid state {sys.argv[1]}, must be a number 0-9")
        sys.exit(1)

print(f"Starting simulator in state {current_state}: {MASLOW_STATES[current_state]}")

# Flask app for HTTP server
app = Flask(__name__)

# WebSocket connections
CONNECTIONS = set()

# ESP800 response with correct websocket address
esp800resp = json.dumps({
    "FWVersion": "3.6.7",
    "FWTarget": "grbl-embedded",
    "FWTargetID": "41",
    "Setup": "Enabled",
    "SDConnection": "direct",
    "SerialProtocol": "Socket",
    "Authentication": "Disabled",
    "WebCommunication": "Synchronous",
    "WebSocketIP": "localhost",
    "WebSocketPort": "8081",
    "Hostname": "maslow",
    "WiFiMode": "STA",
    "WebUpdate": "Enabled",
    "FlashFileSystem": "LittleFS",
    "HostPath": "/",
    "Time": "none",
    "Sensor": json.dumps([
        {
            "P": "/board",
            "H": "/board",
            "T": "S",
            "V": "None",
            "S": "255",
            "M": "0"
        }
    ])
})

@app.route('/')
def index():
    return send_from_directory('dist', 'index.html')

@app.route('/command')
def do_command():
    plainval = request.args.get('plain')
    if plainval == '[ESP800]':
        return esp800resp
    if plainval == '[ESP400]':
        # Return basic settings
        return json.dumps({
            "Settings": [
                {"F": "network/sta_ssid", "P": "0", "T": "S", "V": "test", "H": "SSID", "S": "32", "M": "1"},
                {"F": "network/sta_ip_mode", "P": "0", "T": "B", "V": "1", "H": "IP Mode", "O": [{"DHCP": "1"}, {"Static": "0"}]},
            ]
        })
    return ""

@app.route('/files')
def do_files():
    # Return empty file list
    return json.dumps({
        "files": [],
        "path": "/",
        "occupation": "0",
        "status": "Ok",
        "total": "100000",
        "used": "0"
    })

@app.route('/upload')
def upload():
    # Return empty file list
    return json.dumps({
        "files": [],
        "path": "/",
        "occupation": "0",
        "status": "Ok",
        "total": "100000",
        "used": "0"
    })

# WebSocket message handler
async def register(websocket):
    CONNECTIONS.add(websocket)
    print(f"WebSocket client connected. Total connections: {len(CONNECTIONS)}")

async def unregister(websocket):
    CONNECTIONS.remove(websocket)
    print(f"WebSocket client disconnected. Total connections: {len(CONNECTIONS)}")

async def send_state_message():
    """Send periodic state updates to all connected clients"""
    while True:
        if CONNECTIONS:
            # Send state message in the format the UI expects
            state_msg = f"[MSG:INFO: Current state: {current_state}]"
            minfo_msg = f"MINFO: {json.dumps({'state': current_state, 'homed': True, 'extended': current_state in [4, 7]})}"
            
            disconnected = set()
            for connection in CONNECTIONS:
                try:
                    await connection.send(state_msg)
                    await connection.send(minfo_msg)
                except websockets.exceptions.ConnectionClosed:
                    disconnected.add(connection)
            
            # Remove disconnected clients
            for conn in disconnected:
                CONNECTIONS.discard(conn)
        
        await asyncio.sleep(2)  # Send updates every 2 seconds

async def message_control(websocket, path):
    """Handle WebSocket connections"""
    await register(websocket)
    try:
        # Send initial state
        await websocket.send(f"[MSG:INFO: FluidNC v3.6.7]")
        await websocket.send(f"[MSG:INFO: Current state: {current_state}]")
        await websocket.send(f"MINFO: {json.dumps({'state': current_state, 'homed': True, 'extended': current_state in [4, 7]})}")
        
        # Keep connection alive and handle incoming messages
        async for message in websocket:
            print(f"Received from client: {message}")
            # Echo back or handle commands as needed
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        await unregister(websocket)

async def start_websocket_server():
    """Start the WebSocket server"""
    server = await websockets.serve(
        message_control,
        'localhost',
        8081,
        subprotocols=['arduino']
    )
    print("WebSocket server started on ws://localhost:8081")
    
    # Start the periodic state message task
    asyncio.create_task(send_state_message())
    
    await server.wait_closed()

def run_flask():
    """Run Flask server in a separate thread"""
    print("Starting HTTP server on http://localhost:8080")
    app.run(host='localhost', port=8080, debug=False, use_reloader=False)

if __name__ == '__main__':
    # Start Flask server in a separate thread
    flask_thread = threading.Thread(target=run_flask, daemon=True)
    flask_thread.start()
    
    # Give Flask time to start
    time.sleep(1)
    
    print(f"\n=== Maslow Firmware Simulator ===")
    print(f"State: {current_state} - {MASLOW_STATES[current_state]}")
    print(f"HTTP Server: http://localhost:8080")
    print(f"WebSocket: ws://localhost:8081")
    print(f"\nOpen http://localhost:8080 in your browser")
    print(f"Press Ctrl+C to stop\n")
    
    # Run WebSocket server in the main thread
    try:
        asyncio.run(start_websocket_server())
    except KeyboardInterrupt:
        print("\nShutting down...")
