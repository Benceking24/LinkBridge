#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#define PPQN 24  // MIDI clock sends 24 pulses per quarter note
#define SAMPLE_WINDOW 96  // Number of ticks to average over (4 beats)

static int running = 1;

void signal_handler(int sig) {
    printf("\nReceived SIGINT, shutting down...\n");
    running = 0;
}

// Calculate BPM from interval between ticks
double calculate_bpm(double interval_us) {
    if (interval_us <= 0) return 0.0;
    
    // ticks per second = 1000000 / interval_us
    // beats per second = ticks_per_second / PPQN
    // BPM = beats_per_second * 60
    double ticks_per_second = 1000000.0 / interval_us;
    double beats_per_second = ticks_per_second / PPQN;
    double bpm = beats_per_second * 60.0;
    
    return bpm;
}

int main(int argc, char *argv[]) {
    snd_seq_t *seq_handle;
    int port_id;
    int err;
    snd_seq_event_t *ev;
    
    struct timeval last_tick_time = {0, 0};
    struct timeval current_time;
    int tick_count = 0;
    int beat_count = 0;
    int started = 0;
    
    // Running average of tick intervals
    double tick_intervals[SAMPLE_WINDOW];
    int interval_index = 0;
    int intervals_collected = 0;
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Open ALSA sequencer
    err = snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_INPUT, 0);
    if (err < 0) {
        fprintf(stderr, "Error opening ALSA sequencer: %s\n", snd_strerror(err));
        return 1;
    }
    
    // Set client name
    snd_seq_set_client_name(seq_handle, "MIDI Clock Analyzer");
    
    // Create input port
    port_id = snd_seq_create_simple_port(seq_handle, "MIDI Clock In",
                                          SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                          SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port_id < 0) {
        fprintf(stderr, "Error creating port: %s\n", snd_strerror(port_id));
        snd_seq_close(seq_handle);
        return 1;
    }
    
    printf("MIDI Clock Analyzer started\n");
    printf("Client ID: %d, Port ID: %d\n", snd_seq_client_id(seq_handle), port_id);
    printf("Connect a MIDI clock source to this port using:\n");
    printf("  aconnect <source_client>:<source_port> %d:%d\n", 
           snd_seq_client_id(seq_handle), port_id);
    printf("\nWaiting for MIDI clock data...\n");
    printf("Press Ctrl+C to stop\n\n");
    
    // Initialize tick interval array
    for (int i = 0; i < SAMPLE_WINDOW; i++) {
        tick_intervals[i] = 0.0;
    }
    
    // Main event loop
    while (running) {
        // Wait for event with timeout
        err = snd_seq_event_input(seq_handle, &ev);
        
        if (err == -EAGAIN || err == -ENOSPC) {
            continue;
        }
        
        if (err < 0) {
            if (running) {
                fprintf(stderr, "Error receiving event: %s\n", snd_strerror(err));
            }
            break;
        }
        
        // Get current time
        gettimeofday(&current_time, NULL);
        
        switch (ev->type) {
            case SND_SEQ_EVENT_START:
                printf(">>> MIDI START received\n");
                started = 1;
                tick_count = 0;
                beat_count = 0;
                interval_index = 0;
                intervals_collected = 0;
                last_tick_time.tv_sec = 0;
                last_tick_time.tv_usec = 0;
                break;
                
            case SND_SEQ_EVENT_STOP:
                printf(">>> MIDI STOP received\n");
                printf("Total ticks received: %d\n", tick_count);
                printf("Total beats: %d\n", beat_count);
                started = 0;
                break;
                
            case SND_SEQ_EVENT_CONTINUE:
                printf(">>> MIDI CONTINUE received\n");
                started = 1;
                break;
                
            case SND_SEQ_EVENT_CLOCK:
                if (!started) {
                    printf(">>> MIDI CLOCK received (but not started yet)\n");
                    started = 1;
                }
                
                tick_count++;
                
                // Calculate interval from last tick
                if (last_tick_time.tv_sec != 0) {
                    long interval_us = (current_time.tv_sec - last_tick_time.tv_sec) * 1000000L +
                                      (current_time.tv_usec - last_tick_time.tv_usec);
                    
                    // Store interval in circular buffer
                    tick_intervals[interval_index] = (double)interval_us;
                    interval_index = (interval_index + 1) % SAMPLE_WINDOW;
                    if (intervals_collected < SAMPLE_WINDOW) {
                        intervals_collected++;
                    }
                    
                    // Calculate average interval
                    double avg_interval = 0.0;
                    for (int i = 0; i < intervals_collected; i++) {
                        avg_interval += tick_intervals[i];
                    }
                    avg_interval /= intervals_collected;
                    
                    // Calculate BPM from average interval
                    double bpm = calculate_bpm(avg_interval);
                    
                    // Print status every quarter note (24 ticks)
                    if (tick_count % PPQN == 0) {
                        beat_count++;
                        printf("Beat %4d | Tick %6d | Interval: %7.2f µs | BPM: %6.2f | Avg over %d ticks\n",
                               beat_count, tick_count, (double)interval_us, bpm, intervals_collected);
                    }
                }
                
                last_tick_time = current_time;
                break;
                
            default:
                // Ignore other event types
                break;
        }
        
        snd_seq_free_event(ev);
    }
    
    // Cleanup
    printf("\nCleaning up...\n");
    snd_seq_close(seq_handle);
    printf("MIDI Clock Analyzer stopped\n");
    
    return 0;
}