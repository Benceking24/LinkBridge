#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>

#define BPM 120
#define PPQN 24
#define QUEUE_TEMPO_PPQ 96

// Global handles
static snd_seq_t *seq_handle = NULL;
static int port_id = -1;
static int queue_id = -1;
static snd_seq_tick_time_t current_queue_tick = 0;

// Initialize ALSA sequencer, create port and queue
// Returns 0 on success, -1 on error
int midi_init(void) {
    int err;
    snd_seq_queue_tempo_t *queue_tempo;
    
    // Open ALSA sequencer
    err = snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_OUTPUT, 0);
    if (err < 0) {
        fprintf(stderr, "Error opening ALSA sequencer: %s\n", snd_strerror(err));
        return -1;
    }
    
    // Set client name
    snd_seq_set_client_name(seq_handle, "Python MIDI Clock");
    
    // Create output port
    port_id = snd_seq_create_simple_port(seq_handle, "MIDI Clock Out",
                                          SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                          SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port_id < 0) {
        fprintf(stderr, "Error creating port: %s\n", snd_strerror(port_id));
        snd_seq_close(seq_handle);
        seq_handle = NULL;
        return -1;
    }
    
    // Create queue
    queue_id = snd_seq_alloc_queue(seq_handle);
    if (queue_id < 0) {
        fprintf(stderr, "Error creating queue: %s\n", snd_strerror(queue_id));
        snd_seq_close(seq_handle);
        seq_handle = NULL;
        return -1;
    }
    
    // Set queue tempo (120 BPM)
    snd_seq_queue_tempo_alloca(&queue_tempo);
    snd_seq_queue_tempo_set_tempo(queue_tempo, 500000); // 500000 us per beat (120 BPM)
    snd_seq_queue_tempo_set_ppq(queue_tempo, QUEUE_TEMPO_PPQ);
    err = snd_seq_set_queue_tempo(seq_handle, queue_id, queue_tempo);
    if (err < 0) {
        fprintf(stderr, "Error setting queue tempo: %s\n", snd_strerror(err));
        snd_seq_free_queue(seq_handle, queue_id);
        snd_seq_close(seq_handle);
        seq_handle = NULL;
        return -1;
    }
    
    printf("[C] MIDI initialized: Client %d, Port %d, Queue %d\n", 
           snd_seq_client_id(seq_handle), port_id, queue_id);
    
    current_queue_tick = 0;
    
    return 0;
}

// Send MIDI Start message
// Returns 0 on success, -1 on error
int midi_send_start(void) {
    if (seq_handle == NULL) {
        fprintf(stderr, "Error: MIDI not initialized\n");
        return -1;
    }
    
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, port_id);
    snd_seq_ev_set_subs(&ev);
    ev.type = SND_SEQ_EVENT_START;
    
    snd_seq_ev_schedule_tick(&ev, queue_id, 0, 0);
    snd_seq_event_output(seq_handle, &ev);
    snd_seq_drain_output(seq_handle);
    
    // Start the queue
    snd_seq_start_queue(seq_handle, queue_id, NULL);
    snd_seq_drain_output(seq_handle);
    
    printf("[C] MIDI START sent, queue started\n");
    
    return 0;
}

// Send MIDI Clock message
// Returns 0 on success, -1 on error
int midi_send_clock(void) {
    if (seq_handle == NULL) {
        fprintf(stderr, "Error: MIDI not initialized\n");
        return -1;
    }
    
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, port_id);
    snd_seq_ev_set_subs(&ev);
    ev.type = SND_SEQ_EVENT_CLOCK;
    
    snd_seq_ev_schedule_tick(&ev, queue_id, 0, current_queue_tick);
    snd_seq_event_output(seq_handle, &ev);
    snd_seq_drain_output(seq_handle);
    
    // Advance queue tick by ratio (96 PPQ / 24 PPQN = 4 ticks per MIDI clock)
    current_queue_tick += (QUEUE_TEMPO_PPQ / PPQN);
    
    return 0;
}

// Send MIDI Stop message
// Returns 0 on success, -1 on error
int midi_send_stop(void) {
    if (seq_handle == NULL) {
        fprintf(stderr, "Error: MIDI not initialized\n");
        return -1;
    }
    
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, port_id);
    snd_seq_ev_set_subs(&ev);
    ev.type = SND_SEQ_EVENT_STOP;
    
    snd_seq_ev_schedule_tick(&ev, queue_id, 0, current_queue_tick);
    snd_seq_event_output(seq_handle, &ev);
    snd_seq_drain_output(seq_handle);
    
    printf("[C] MIDI STOP sent\n");
    
    return 0;
}

// Get current tick count
unsigned int midi_get_tick_count(void) {
    return current_queue_tick;
}

// Cleanup and close ALSA sequencer
void midi_cleanup(void) {
    if (seq_handle != NULL) {
        if (queue_id >= 0) {
            snd_seq_stop_queue(seq_handle, queue_id, NULL);
            snd_seq_free_queue(seq_handle, queue_id);
        }
        snd_seq_close(seq_handle);
        seq_handle = NULL;
        port_id = -1;
        queue_id = -1;
        printf("[C] MIDI cleanup complete\n");
    }
}

// Get client ID
int midi_get_client_id(void) {
    if (seq_handle == NULL) return -1;
    return snd_seq_client_id(seq_handle);
}

// Get port ID
int midi_get_port_id(void) {
    return port_id;
}

// Get queue ID
int midi_get_queue_id(void) {
    return queue_id;
}