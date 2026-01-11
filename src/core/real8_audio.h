#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

class Real8VM;
class IReal8Host;

struct AudioStateSnapshot;

// 1. Define Channel State (Shared by Main and Child channels)
struct ChannelState {
    int sfx_id = -1;
    float offset = 0; 
    int last_note_idx = -1;

    // Oscillators
    float phi = 0;       
    
    // Noise
    uint32_t lfsr = 0x7FFF; 
    float noise_sample = 0; 

    // Playback State
    float current_vol = 0;
    float current_pitch_val = 0; 
    float slide_start_pitch = 0; 
    float vib_phase = 0;
    
    // Loop
    int loop_start = 0;
    int loop_end = 0;
    bool loop_active = false;
    int stop_row = -1;
    
    // Timing
    int tick_counter = 0;
    int speed = 1;
    int row = 0;
};

// 2. Define Channel (Inherits State + Child)
struct Channel : public ChannelState
{
    // A channel can have a "child" channel for playing Custom Instruments (SFX 0-7)
    ChannelState child; 
    bool has_child = false;

    bool is_music = false;
    
    // Effect State
    int effect = 0;
    int param = 0; 

    // For crossfading (anti-pop) - Reserved for future use
    ChannelState prev_state; 
    bool is_crossfading = false;
    float crossfade_progress = 0.0f;
};

struct AudioEngine
{
    // "muted" is reserved for an explicit/host-driven mute (if any).
    // "volume_mute" is an automatic hard gate used when BOTH MUSIC and SFX
    // master volumes are set to 0 in the in-game menu.
    bool muted = false;
    bool volume_mute = false;
    static const int CHANNELS = 4;
    static const int SAMPLE_RATE = 22050;
    static constexpr int SNAP_COUNT = 256;

    struct MixerTickSnap {
        int sfx_id[CHANNELS];
        int note_row[CHANNELS];
        int music_pattern;
        int patterns_played;
        int ticks_on_pattern;
        bool music_playing;
    };
    
    // PICO-8 update rate is 120Hz (approx 183.75 samples per tick)
    float samples_per_tick_accumulator = 0.0f; 

    // Buffer output
    float samples_accumulator = 0.0f;
    float last_mixed_sample = 0.0f;
    
    // DECLARE CHANNELS HERE
    Channel channels[CHANNELS];

    // FIX: Buffer size
    int16_t buffer[2048]; 
    
    Real8VM *vm = nullptr;

    // Music State
    int music_pattern = -1;
    int music_tick_timer = 0; 
    int music_speed = 1;
    int music_loop_start = -1;
    uint8_t music_mask = 0;
    bool music_playing = false;
    int music_patterns_played = 0;
    int music_ticks_on_pattern = 0;

    MixerTickSnap snaps[SNAP_COUNT];
    int snap_w = 0;
    bool snaps_ready = false;

    // Returns true if audio is hard-gated (either explicitly muted or auto-muted).
    inline bool isMuted() const { return muted || volume_mute; }

    // Auto-mute gate: when BOTH master volumes are 0, stop all audio work.
    // This prevents the audio engine from reading SFX/MUSIC data or advancing
    // sequencer state until at least one master volume is > 0 again.
    void updateVolumeMute();
    void flushOutputQueues();

    void init(Real8VM *parent);
    void play_sfx(int idx, int ch, int offset = 0, int length = -1);
    void play_music(int t, int fade_len = 0, int mask = 0x0f);
    
    // Generate samples to specific buffer (Libretro)
    void generateSamples(int16_t* out_buffer, int samples_to_generate);

    // Main Update
    void update(IReal8Host *host);
    
    // Internal Tickers
    void run_tick();           
    void update_channel_tick(int ch_idx); 
    void update_music_tick(); 

    // Updated Signature
    float get_waveform_sample(int waveform, float phi, ChannelState &state, float freq_mult);
    float note_to_freq(float note);
    
    // Inline getters now have visibility of 'channels'
    int get_sfx_id(int ch) const { return (ch>=0 && ch<4) ? channels[ch].sfx_id : -1; }
    int get_note(int ch) const {
        if (ch < 0 || ch >= 4) return -1;
        if (channels[ch].sfx_id == -1) return -1;
        return (channels[ch].last_note_idx >= 0) ? channels[ch].last_note_idx : channels[ch].row;
    }
    int get_note_row(int ch) const {
        if (ch < 0 || ch >= 4) return -1;
        const Channel &c = channels[ch];
        if (c.sfx_id == -1) return -1;
        return (c.last_note_idx < 0) ? 0 : c.last_note_idx;
    }
    int get_music_pattern() const { return music_pattern; }
    int get_music_row() const { return channels[0].is_music ? channels[0].row : 0; } 
    int get_music_speed() const { return music_speed; }
    int get_music_patterns_played() const { return music_patterns_played; }
    int get_music_ticks_on_pattern() const { return music_ticks_on_pattern; }
    bool is_music_playing() const { return music_playing; }

    int get_sfx_id_hp(int ch) const;
    int get_note_row_hp(int ch) const;
    int get_music_pattern_hp() const;
    int get_music_patterns_played_hp() const;
    int get_music_ticks_on_pattern_hp() const;
    bool is_music_playing_hp() const;

    AudioStateSnapshot getState();
    void setState(const AudioStateSnapshot& s);

    // 3DS / GBA
#if defined(__GBA__)
    static constexpr int OUT_BLOCK_SAMPLES = 368;
    static constexpr int OUT_BLOCK_RING    = 4;
    static constexpr int FIFO_SAMPLES      = OUT_BLOCK_SAMPLES * 8;
#else
    static constexpr int OUT_BLOCK_SAMPLES = 1024;
    static constexpr int OUT_BLOCK_RING    = 4;
    static constexpr int FIFO_SAMPLES      = OUT_BLOCK_SAMPLES * 8;
#endif

    int16_t fifo[FIFO_SAMPLES];
    int fifo_r = 0, fifo_w = 0, fifo_count = 0;

    int16_t out_blocks[OUT_BLOCK_RING][OUT_BLOCK_SAMPLES];
    int out_block_idx = 0;

};

struct AudioStateSnapshot {
    Channel channels[4];
    int music_pattern;
    int music_tick_timer;
    int music_speed;
    int music_loop_start;
    uint8_t music_mask;
    bool music_playing;
    int music_patterns_played;
    int music_ticks_on_pattern;
};
