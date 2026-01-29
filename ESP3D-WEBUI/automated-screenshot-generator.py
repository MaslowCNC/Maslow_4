#!/usr/bin/env python3
"""
Automated screenshot generator for all 10 Maslow states using browser automation
This script will be called by the main agent to generate screenshots
"""

import subprocess
import time
import sys

def generate_screenshots_for_state(state_num, state_name):
    """
    This function starts the simulator for a given state and returns.
    The actual screenshot will be taken by the playwright tools.
    """
    print(f"Starting simulator for state {state_num}: {state_name}")
    
    # Start the simulator
    proc = subprocess.Popen([
        sys.executable,
        'firmware-simulator.py',
        str(state_num)
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for servers to start
    time.sleep(3)
    
    return proc

def stop_simulator(proc):
    """Stop the simulator process"""
    if proc:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        time.sleep(1)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python automated-screenshot-generator.py <state_number>")
        print("state_number: 0-9")
        sys.exit(1)
    
    state_num = int(sys.argv[1])
    if state_num < 0 or state_num > 9:
        print("State number must be 0-9")
        sys.exit(1)
    
    STATES = {
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
    
    proc = generate_screenshots_for_state(state_num, STATES[state_num])
    
    # Keep running until interrupted
    try:
        proc.wait()
    except KeyboardInterrupt:
        stop_simulator(proc)
