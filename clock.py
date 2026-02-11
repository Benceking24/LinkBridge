#!/usr/bin/env python3

import ctypes
import time
import signal
import sys
import os

# Constants
BPM = 120
PPQN = 24  # Pulses Per Quarter Note

# Global flag for clean shutdown
running = True

def signal_handler(sig, frame):
    """Handle SIGINT (Ctrl+C) for clean shutdown"""
    global running
    print("\n[Python] Received SIGINT, shutting down...")
    running = False

def calculate_tick_interval():
    """Calculate the interval between MIDI clock ticks in seconds"""
    # At 120 BPM with 24 PPQN: 48 ticks/second
    ticks_per_second = (BPM / 60.0) * PPQN
    return 1.0 / ticks_per_second

def main():
    global running
    
    # Setup signal handler
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Load the C library
    lib_path = os.path.join(os.path.dirname(__file__), 'libmidiclock.so')
    
    if not os.path.exists(lib_path):
        print(f"Error: Library not found at {lib_path}")
        print("Please compile the library first:")
        print("  gcc -shared -fPIC -o libmidiclock.so midi_clock_lib.c -lasound")
        return 1
    
    try:
        midi_lib = ctypes.CDLL(lib_path)
    except OSError as e:
        print(f"Error loading library: {e}")
        return 1
    
    # Define function prototypes
    midi_lib.midi_init.restype = ctypes.c_int
    midi_lib.midi_send_start.restype = ctypes.c_int
    midi_lib.midi_send_clock.restype = ctypes.c_int
    midi_lib.midi_send_stop.restype = ctypes.c_int
    midi_lib.midi_get_tick_count.restype = ctypes.c_uint
    midi_lib.midi_get_client_id.restype = ctypes.c_int
    midi_lib.midi_get_port_id.restype = ctypes.c_int
    midi_lib.midi_get_queue_id.restype = ctypes.c_int
    midi_lib.midi_cleanup.restype = None
    
    print("[Python] Python MIDI Clock Generator")
    print("[Python] ============================")
    print(f"[Python] BPM: {BPM}, PPQN: {PPQN}")
    print()
    
    # Initialize MIDI
    print("[Python] Initializing ALSA MIDI...")
    if midi_lib.midi_init() < 0:
        print("[Python] Error: Failed to initialize MIDI")
        return 1
    
    client_id = midi_lib.midi_get_client_id()
    port_id = midi_lib.midi_get_port_id()
    queue_id = midi_lib.midi_get_queue_id()
    
    print(f"[Python] ALSA Client ID: {client_id}")
    print(f"[Python] ALSA Port ID: {port_id}")
    print(f"[Python] ALSA Queue ID: {queue_id}")
    print(f"[Python] Connect with: aconnect {client_id}:{port_id} <destination>")
    print()
    print("[Python] Press Ctrl+C to stop")
    print()
    
    # Send MIDI Start
    if midi_lib.midi_send_start() < 0:
        print("[Python] Error: Failed to send MIDI START")
        midi_lib.midi_cleanup()
        return 1
    
    # Calculate tick interval
    tick_interval = calculate_tick_interval()
    print(f"[Python] Tick interval: {tick_interval*1000:.3f} ms ({1/tick_interval:.1f} ticks/sec)")
    print()
    
    # Get start time for accurate timing
    next_tick_time = time.monotonic()
    tick_count = 0
    beat_count = 0
    
    # Main loop - send MIDI clock ticks
    try:
        while running:
            # Send MIDI Clock
            if midi_lib.midi_send_clock() < 0:
                print("[Python] Error: Failed to send MIDI CLOCK")
                break
            
            tick_count += 1
            
            # Print status every quarter note (24 ticks = 1 beat)
            if tick_count % PPQN == 0:
                beat_count += 1
                queue_tick = midi_lib.midi_get_tick_count()
                print(f"[Python] Beat {beat_count:4d} | MIDI Tick {tick_count:6d} | Queue Tick {queue_tick:6d}")
            
            # Sleep until next tick using absolute time to prevent drift
            next_tick_time += tick_interval
            sleep_time = next_tick_time - time.monotonic()
            
            if sleep_time > 0:
                time.sleep(sleep_time)
            else:
                # We're running behind - don't sleep, just continue
                # Reset next_tick_time to current time to resync
                if sleep_time < -tick_interval:
                    next_tick_time = time.monotonic()
    
    except Exception as e:
        print(f"[Python] Error in main loop: {e}")
    
    # Cleanup
    print()
    print("[Python] Stopping MIDI clock...")
    
    # Send MIDI Stop
    midi_lib.midi_send_stop()
    
    # Small delay to let the stop message be delivered
    time.sleep(0.1)
    
    # Cleanup ALSA resources
    midi_lib.midi_cleanup()
    
    print(f"[Python] Total ticks sent: {tick_count}")
    print(f"[Python] Total beats: {beat_count}")
    print("[Python] Shutdown complete")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())