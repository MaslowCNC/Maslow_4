#!/usr/bin/env python3
"""
Generate screenshots of all 10 Maslow states
"""

import subprocess
import time
import os
import sys

# Maslow states
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

def main():
    print("Maslow State Screenshot Generator")
    print("=" * 50)
    
    # Check if dist/index.html exists
    if not os.path.exists('dist/index.html'):
        print("ERROR: dist/index.html not found")
        print("Please run 'gulp package --lang en' first")
        sys.exit(1)
    
    print("Found dist/index.html")
    print("\nThis script will:")
    print("1. Start firmware simulator for each state")
    print("2. Wait for you to capture a screenshot")
    print("3. Move to the next state")
    print("\nYou need to manually:")
    print("- Open http://localhost:8080 in your browser")
    print("- Wait for the interface to load and show the state")
    print("- Take a screenshot")
    print("- Press Enter in this terminal to continue to next state")
    print("\n" + "=" * 50)
    
    for state_num in range(10):
        state_name = STATES[state_num]
        print(f"\n\n{'='*50}")
        print(f"STATE {state_num}: {state_name}")
        print(f"{'='*50}")
        
        print(f"Starting simulator in state {state_num}...")
        
        # Start the simulator for this state
        proc = subprocess.Popen([
            sys.executable,
            'firmware-simulator.py',
            str(state_num)
        ])
        
        print(f"\nSimulator running for state: {state_name}")
        print(f"1. Open http://localhost:8080 in your browser")
        print(f"2. Click on the 'Maslow' tab to see the state")
        print(f"3. Wait for the state to update (2-3 seconds)")
        print(f"4. Take a screenshot")
        print(f"5. Press Enter when ready for next state...")
        
        input()  # Wait for user to press Enter
        
        # Kill the simulator
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        
        print(f"Stopped simulator for state {state_num}")
        time.sleep(1)  # Brief pause between states
    
    print(f"\n{'='*50}")
    print("Screenshot generation complete!")
    print("All 10 states have been shown.")
    print(f"{'='*50}\n")

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        sys.exit(0)
