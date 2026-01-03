#include "real8_audio.h"
#include "real8_vm.h"
#include "../hal/real8_host.h"

#include <cmath>
#include <algorithm>
#include <cstring>

// --------------------------------------------------------------------------
// CONSTANTS
// --------------------------------------------------------------------------

static const float PI = 3.1415926535f;
static const float TWO_PI = 6.2831853071f;
static constexpr float kInvSampleRate = 1.0f / (float)AudioEngine::SAMPLE_RATE;
static constexpr float kSamplesPerTick = (float)AudioEngine::SAMPLE_RATE / 120.0f;
static constexpr float kC2Freq = 65.406f;
static constexpr float kVibFreq = 15.0f;
static constexpr float kVibAmp = 0.25f;
static constexpr int kVibLutSize = 256;
static constexpr int kVibLutMask = kVibLutSize - 1;
static constexpr float kVibLutScale = 1.0f / 32767.0f;

static float g_noteFreqLut[65];
static int16_t g_vibSinLut[kVibLutSize];
static float g_vibPhaseStep = 0.0f;
static bool g_audioLutReady = false;

#if defined(__GBA__)
static inline float fast_sin_gba(float x) {
    if (x > PI) x -= TWO_PI;
    const float B = 4.0f / PI;
    const float C = -4.0f / (PI * PI);
    float y = B * x + C * x * ((x < 0.0f) ? -x : x);
    const float P = 0.225f;
    return P * (y * ((y < 0.0f) ? -y : y) - y) + y;
}
#endif

static void initAudioLuts() {
    if (g_audioLutReady) return;
#if defined(__GBA__)
    constexpr float kSemitoneRatio = 1.05946309436f;
    float freq = kC2Freq;
    for (int i = 0; i <= 64; ++i) {
        g_noteFreqLut[i] = freq;
        freq *= kSemitoneRatio;
    }
    for (int i = 0; i < kVibLutSize; ++i) {
        float angle = (TWO_PI * (float)i) / (float)kVibLutSize;
        g_vibSinLut[i] = (int16_t)(fast_sin_gba(angle) * 32767.0f);
    }
#else
    for (int i = 0; i <= 64; ++i) {
        g_noteFreqLut[i] = kC2Freq * powf(2.0f, (float)i / 12.0f);
    }
    for (int i = 0; i < kVibLutSize; ++i) {
        float angle = (TWO_PI * (float)i) / (float)kVibLutSize;
        g_vibSinLut[i] = (int16_t)(sinf(angle) * 32767.0f);
    }
#endif
    g_vibPhaseStep = kVibFreq * kInvSampleRate;
    g_audioLutReady = true;
}

// --------------------------------------------------------------------------
// WAVEFORM GENERATION
// --------------------------------------------------------------------------

static inline float wrap01_fast(float t) {
    if (t >= 1.0f) t -= 1.0f;
    if (t < 0.0f) t += 1.0f;
    return t;
}

// 0. Triangle
static float osc_tri_unit(float t) {
    return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
}

// 1. Tilted Saw
static float osc_tilted_saw_unit(float t) {
    const float k = 0.875f; 
    if (t < k) return (2.0f * t / k) - 1.0f;
    return (1.0f - 2.0f * (t - k) / (1.0f - k)) * -1.0f;
}

// 2. Sawtooth
static float osc_saw_unit(float t) {
    return 2.0f * t - 1.0f;
}

// 3. Square
static float osc_square_unit(float t) {
    return (t < 0.5f) ? 1.0f : -1.0f;
}

// 4. Pulse (25% Duty)
static float osc_pulse_unit(float t) {
    return (t < 0.25f) ? 1.0f : -1.0f;
}

// 5. Organ: Triangle + Octave (2nd Harmonic)
static float osc_organ(float t) {
    float t2 = t + t;
    if (t2 >= 1.0f) t2 -= 1.0f;
    return (osc_tri_unit(t) + osc_tri_unit(t2)) * 0.5f;
}

// 7. Phaser
static float osc_phaser(float t) {
    float modulator = osc_tri_unit(t * (1.0f / 64.0f)); 
    float phase_dist = t + (modulator * (2.0f / 3.0f)); 
    phase_dist = wrap01_fast(phase_dist);
    return osc_tri_unit(phase_dist);
}

static inline float note_to_freq_fast(float note) {
    if (!g_audioLutReady) initAudioLuts();
    if (note <= 0.0f) return g_noteFreqLut[0];
    if (note >= 63.0f) return g_noteFreqLut[63];
    int idx = (int)note;
    if (idx >= 63) return g_noteFreqLut[63];
    float frac = note - (float)idx;
    float f0 = g_noteFreqLut[idx];
    float f1 = g_noteFreqLut[idx + 1];
    return f0 + (f1 - f0) * frac;
}

// --------------------------------------------------------------------------
// INTERNAL HELPER: Single State Sample Generation
// --------------------------------------------------------------------------

static float get_sample_for_state(ChannelState &state, int waveform, float freq) {
    // 1. Update Phase Accumulator
    float dt = freq * kInvSampleRate;
    float old_phi = state.phi;
    state.phi += dt;
    
    if (state.phi >= 1.0f) {
        state.phi -= 1.0f;
    }

    // 2. Handle Noise (Waveform 6)
    if (waveform == 6) {
        if (state.phi < old_phi) {
             uint32_t b = (state.lfsr & 1) ^ ((state.lfsr >> 1) & 1);
             state.lfsr = (state.lfsr >> 1) | (b << 14);
             state.noise_sample = (state.lfsr & 1) ? 1.0f : -1.0f;
        }
        return state.noise_sample;
    }

    // 3. Generate Standard Waveforms
    switch (waveform) {
        case 0: return osc_tri_unit(state.phi);
        case 1: return osc_tilted_saw_unit(state.phi);
        case 2: return osc_saw_unit(state.phi);
        case 3: return osc_square_unit(state.phi);
        case 4: return osc_pulse_unit(state.phi);
        case 5: return osc_organ(state.phi);
        case 7: return osc_phaser(state.phi);
        default: return 0.0f;
    }
}

// --------------------------------------------------------------------------
// AUDIO ENGINE IMPLEMENTATION
// --------------------------------------------------------------------------

void AudioEngine::init(Real8VM *parent) { 
    vm = parent; 
    initAudioLuts();

    for (int i = 0; i < SNAP_COUNT; i++) {
        for (int c = 0; c < CHANNELS; c++) {
            snaps[i].sfx_id[c] = -1;
            snaps[i].note_row[c] = -1;
        }
        snaps[i].music_pattern = -1;
        snaps[i].patterns_played = 0;
        snaps[i].ticks_on_pattern = 0;
        snaps[i].music_playing = false;
    }
}

float AudioEngine::note_to_freq(float note) {
    return note_to_freq_fast(note);
}

// Stub for the interface method (now handled by internal helper)
float AudioEngine::get_waveform_sample(int waveform, float phi, ChannelState &state, float freq_mult) {
    // This is kept for compatibility if called externally.
    // Use the provided phase and avoid mutating state or interpreting freq_mult as Hz.
    (void)freq_mult;
    switch (waveform) {
        case 0: return osc_tri_unit(phi);
        case 1: return osc_tilted_saw_unit(phi);
        case 2: return osc_saw_unit(phi);
        case 3: return osc_square_unit(phi);
        case 4: return osc_pulse_unit(phi);
        case 5: return osc_organ(phi);
        case 6: return state.noise_sample;
        case 7: return osc_phaser(phi);
        default: return 0.0f;
    }
}

void AudioEngine::play_sfx(int idx, int ch, int offset, int length) {
    if (muted) return;
    if (idx < -2 || idx > 63) return;
    if (ch < -2 || ch >= CHANNELS) return;

    auto stop_channel = [this](int i) {
        if (i < 0 || i >= CHANNELS) return;
        channels[i].sfx_id = -1;
        channels[i].current_vol = 0;
        channels[i].current_pitch_val = 0;
        channels[i].last_note_idx = -1;
        channels[i].stop_row = -1;
    };

    if (ch == -2) {
        for (int i = 0; i < CHANNELS; i++) {
            if (channels[i].sfx_id == idx) {
                stop_channel(i);
            }
        }
        return;
    }

    if (idx == -1) {
        if (ch == -1) {
            for (int i = 0; i < CHANNELS; i++) stop_channel(i);
        } else {
            stop_channel(ch);
        }
        return;
    }

    if (idx == -2) {
        if (ch == -1) {
            for (int i = 0; i < CHANNELS; i++) {
                channels[i].loop_active = false;
            }
        } else if (ch >= 0 && ch < CHANNELS) {
            channels[ch].loop_active = false;
        }
        return;
    }

    int target_ch = -1;
    if (ch >= 0 && ch < CHANNELS) {
        target_ch = ch;
    } else {
        for (int i = 0; i < CHANNELS; i++) {
            if (channels[i].sfx_id == -1) { target_ch = i; break; }
        }
        if (target_ch == -1) {
            for (int i = 0; i < CHANNELS; i++) {
                if (!channels[i].is_music) { target_ch = i; break; }
            }
        }
        if (target_ch == -1) target_ch = 3; 
    }

    Channel &c = channels[target_ch];
    
    c.sfx_id = idx;
    c.is_music = (ch >= 0 && music_playing); 
    c.row = std::clamp(offset, 0, 31); 
    c.phi = 0.0f; 
    c.lfsr = 0x5205; 
    c.noise_sample = 0.0f;
    c.current_vol = 0; 
    c.current_pitch_val = 0; 
    c.last_note_idx = -1;
    c.stop_row = (length >= 0) ? std::min(32, c.row + length) : -1;
    
    // Reset Child
    c.child.sfx_id = -1;

    uint8_t *sfx_data = vm->sfx_ram + (idx * 68);
    c.speed = std::max(1, (int)sfx_data[65]);
    c.loop_start = sfx_data[66];
    c.loop_end = sfx_data[67];
    c.loop_active = (c.loop_end > c.loop_start);
    c.tick_counter = 1; 
}

void AudioEngine::play_music(int pattern, int fade_len, int mask) {
    (void)fade_len;
    music_patterns_played = 0;
    music_ticks_on_pattern = 0;
    if (pattern < 0) {
        music_playing = false;
        music_pattern = -1;
        return;
    }
    music_pattern = pattern;
    music_playing = true;
    music_tick_timer = 1; 
    music_loop_start = -1;
    music_mask = (uint8_t)((mask == 0) ? 0x0f : (mask & 0x0f));

    if (pattern < 64) {
        uint8_t m0 = vm->music_ram[pattern * 4 + 0];
        if (m0 & 0x80) music_loop_start = pattern;
    }
    
    for(int i=0; i<4; i++) {
        if (music_mask & (1<<i)) {
            channels[i].sfx_id = -1; 
            channels[i].is_music = true;
        } else {
            channels[i].is_music = false;
        }
    }
}

// --------------------------------------------------------------------------
// TICK SYSTEM
// --------------------------------------------------------------------------

void AudioEngine::update_music_tick() {
    if (!music_playing || music_pattern < 0) return;

    if (music_tick_timer > 0) {
        music_tick_timer--;
    }

    int total_ticks = 32 * std::max(1, music_speed);
    int elapsed = total_ticks - music_tick_timer;
    if (elapsed < 0) elapsed = 0;
    if (elapsed >= total_ticks) elapsed = total_ticks - 1;
    music_ticks_on_pattern = (music_speed > 0) ? (elapsed / music_speed) : 0;
    if (music_tick_timer > 0) return;

    music_patterns_played++;
    music_ticks_on_pattern = 0;

    int ram_addr = music_pattern * 4;
    uint8_t m[4];
    for(int i=0; i<4; i++) m[i] = vm->music_ram[ram_addr + i];

    int fastest_speed = 0;

    for (int i = 0; i < 4; i++) {
        if ((music_mask & (1 << i)) == 0) continue;

        int sfx = m[i] & 0x3F;
        bool empty = (m[i] & 0x40) || (sfx > 63);

        if (!empty) {
            int spd = std::max(1, (int)vm->sfx_ram[sfx * 68 + 65]);
            if (fastest_speed == 0 || spd < fastest_speed) fastest_speed = spd;
            play_sfx(sfx, i);
        }
    }
    
    if (fastest_speed == 0) fastest_speed = 1;
    music_speed = fastest_speed;
    music_tick_timer = 32 * music_speed;

    bool loop_start = (m[0] & 0x80);
    bool loop_back  = (m[1] & 0x80);
    bool stop       = (m[2] & 0x80);

    if (loop_start) music_loop_start = music_pattern;

    if (loop_back) { 
        music_pattern = (music_loop_start != -1) ? music_loop_start : 0;
    } else if (stop) { 
        music_playing = false;
        music_pattern = -1;
    } else {
        music_pattern++;
        if (music_pattern >= 64) music_pattern = -1;
    }
}

void AudioEngine::update_channel_tick(int idx) {
    Channel &c = channels[idx];
    if (c.sfx_id == -1) return;

    c.tick_counter--;
    if (c.tick_counter > 0) return;

    c.tick_counter = c.speed;

    uint8_t *sfx = vm->sfx_ram + (c.sfx_id * 68);
    int addr = c.row * 2;
    uint8_t b0 = sfx[addr];
    uint8_t b1 = sfx[addr+1];

    int pitch_key = b0 & 0x3F;
    int vol       = (b1 >> 2) & 0x7;
    int effect = (b1 & 0x3) | ((b0 & 0x40) >> 4);

    float target_pitch = (float)pitch_key;

    if (vol > 0) {
        bool is_slide = (effect == 1);
        if (is_slide) {
            c.slide_start_pitch = c.current_pitch_val;
        } else {
            c.current_pitch_val = target_pitch;
            c.slide_start_pitch = target_pitch;
        }
        c.current_vol = (float)vol;
    } else {
        c.current_vol = 0;
    }

    c.effect = effect;
    
    c.last_note_idx = c.row;
    c.row++;
    if (c.stop_row >= 0 && c.row >= c.stop_row) {
        c.sfx_id = -1;
        c.stop_row = -1;
        return;
    }
    if (c.row >= 32) {
        if (c.loop_active) {
            c.row = c.loop_start;
        } else {
            c.sfx_id = -1;
        }
    }
}

void AudioEngine::run_tick() {
    update_music_tick();
    for (int i = 0; i < 4; i++) {
        update_channel_tick(i);
    }

    MixerTickSnap &s = snaps[snap_w++ & (SNAP_COUNT - 1)];
    for (int i = 0; i < 4; i++) {
        s.sfx_id[i] = channels[i].sfx_id;
        if (channels[i].sfx_id == -1) {
            s.note_row[i] = -1;
        } else {
            s.note_row[i] = (channels[i].last_note_idx < 0) ? 0 : channels[i].last_note_idx;
        }
    }
    s.music_pattern = music_pattern;
    s.patterns_played = music_patterns_played;
    s.ticks_on_pattern = music_ticks_on_pattern;
    s.music_playing = music_playing;
    snaps_ready = true;
}

// --------------------------------------------------------------------------
// MAIN GENERATION LOOP
// --------------------------------------------------------------------------

void AudioEngine::generateSamples(int16_t* out_buffer, int count) {
    if (muted) {
        memset(out_buffer, 0, count * sizeof(int16_t));
        return;
    }

    for (int i = 0; i < count; i++) {
        
        // --- 1. SEQUENCER UPDATE ---
        samples_per_tick_accumulator += 1.0f;
        while (samples_per_tick_accumulator >= kSamplesPerTick) {
            samples_per_tick_accumulator -= kSamplesPerTick;
            run_tick();
        }

        float mixed_sample = 0.0f;

        // --- 2. SYNTHESIZE CHANNELS ---
        for (int c = 0; c < CHANNELS; c++) {
            Channel &ch = channels[c];
            if (ch.sfx_id == -1) continue;

            // Get Note Data
            uint8_t *sfx_data = vm->sfx_ram + (ch.sfx_id * 68);
            int row_idx = (ch.last_note_idx < 0) ? 0 : ch.last_note_idx; // Use correct note index for playback
            int addr = row_idx * 2;
            uint8_t b1 = sfx_data[addr+1];
            int waveform = (b1 >> 5) & 0x7;
            int pitch_key = sfx_data[addr] & 0x3F;
            
            // --- FX PROCESSING ---
            float pitch = ch.current_pitch_val;
            float vol = (float)ch.current_vol / 7.0f;
            
            float progress = 0.0f;
            if (ch.speed > 0) progress = 1.0f - ((float)ch.tick_counter / (float)ch.speed);

            switch(ch.effect) {
                case 1: // Slide
                    pitch = ch.slide_start_pitch + (float(pitch_key) - ch.slide_start_pitch) * progress;
                    break;
                case 2: // Vibrato
                    ch.vib_phase += g_vibPhaseStep;
                    if (ch.vib_phase >= 1.0f) ch.vib_phase -= 1.0f;
                    {
                        int vib_idx = (int)(ch.vib_phase * kVibLutSize) & kVibLutMask;
                        float vib = (float)g_vibSinLut[vib_idx] * kVibLutScale;
                        pitch += vib * kVibAmp;
                    }
                    break;
                case 3: // Drop
                    pitch = ch.slide_start_pitch * (1.0f - progress); 
                    break;
                case 4: // Fade In
                    vol *= progress;
                    break;
                case 5: // Fade Out
                    vol *= (1.0f - progress);
                    break;
            }

            // --- WAVEFORM GENERATION ---
            float sample = 0.0f;
            float freq = note_to_freq(pitch);

            if (waveform > 7) { 
                // --- CUSTOM INSTRUMENT (8-15) ---
                int child_sfx_id = waveform - 8;
                
                if (ch.child.sfx_id != child_sfx_id) {
                    ch.child.sfx_id = child_sfx_id;
                    ch.child.phi = 0;
                    ch.child.offset = 0;
                    ch.child.lfsr = 0x5205; 
                }

                float playback_rate = freq / kC2Freq;

                float row_per_sample = (1.0f / 183.0f) * playback_rate; 
                ch.child.offset += row_per_sample;
                if (ch.child.offset >= 32.0f) ch.child.offset -= 32.0f; 

                int child_row = (int)ch.child.offset;
                uint8_t* child_data = vm->sfx_ram + (child_sfx_id * 68);
                int child_addr = child_row * 2;
                
                int child_wave = (child_data[child_addr+1] >> 5) & 0x7; 
                int child_vol_raw = (child_data[child_addr+1] >> 1) & 0x7;
                int child_key = child_data[child_addr] & 0x3F;

                float child_freq = note_to_freq(child_key) * playback_rate;
                float raw_child_sample = get_sample_for_state(ch.child, child_wave, child_freq);
                
                sample = raw_child_sample * ((float)child_vol_raw / 7.0f);

            } else {
                // --- STANDARD INSTRUMENT (0-7) ---
                sample = get_sample_for_state(ch, waveform, freq);
            }

            if (ch.is_music) {
                float music_master = (float)vm->volume_music / 10.0f;
                vol *= (0.6f * music_master); 
            } else {
                float sfx_master = (float)vm->volume_sfx / 10.0f;
                vol *= sfx_master;
            }

            mixed_sample += sample * vol;
        }

        // --- 3. HARDWARE DISTORTION ---
        if (vm->hwState.distort > 0) {
             float dist_val = mixed_sample * 0.5f; 
             int16_t d = (int16_t)(dist_val * 32767.0f);
             d = (d / 0x1000) * 0x1249; 
             mixed_sample = (float)d / 32767.0f;
        }

        // --- 4. OUTPUT ---
        float out = mixed_sample * 0.5f; 
        if (out < -1.0f) out = -1.0f; else if (out > 1.0f) out = 1.0f;
        out_buffer[i] = (int16_t)(out * 32767.0f);
    }
}

void AudioEngine::update(IReal8Host *host) {
    if (!host) return;

    samples_accumulator += (float)SAMPLE_RATE / 60.0f;
    int gen = (int)samples_accumulator;
    samples_accumulator -= (float)gen;

    if (gen <= 0) return;
    if (gen > 2048) gen = 2048;

    // Generate into scratch
    generateSamples(buffer, gen);

    // FIFO write
    auto fifo_write = [&](const int16_t* in, int n) {
        for (int i = 0; i < n; i++) {
            if (fifo_count >= FIFO_SAMPLES) break; // drop if overflow
            fifo[fifo_w] = in[i];
            fifo_w = (fifo_w + 1) % FIFO_SAMPLES;
            fifo_count++;
        }
    };

    auto fifo_read_block = [&](int16_t* out, int n) -> bool {
        if (fifo_count < n) return false;
        for (int i = 0; i < n; i++) {
            out[i] = fifo[fifo_r];
            fifo_r = (fifo_r + 1) % FIFO_SAMPLES;
            fifo_count--;
        }
        return true;
    };

    fifo_write(buffer, gen);

    // Push in stable, fixed blocks
    while (fifo_count >= OUT_BLOCK_SAMPLES) {
        int16_t* out = out_blocks[out_block_idx];
        if (!fifo_read_block(out, OUT_BLOCK_SAMPLES)) break;

        host->pushAudio(out, OUT_BLOCK_SAMPLES);

        out_block_idx = (out_block_idx + 1) % OUT_BLOCK_RING;
    }
}

AudioStateSnapshot AudioEngine::getState() {
    AudioStateSnapshot s;
    for(int i=0; i<4; i++) s.channels[i] = channels[i];
    s.music_pattern = music_pattern;
    s.music_tick_timer = music_tick_timer;
    s.music_speed = music_speed;
    s.music_loop_start = music_loop_start;
    s.music_mask = music_mask;
    s.music_playing = music_playing;
    s.music_patterns_played = music_patterns_played;
    s.music_ticks_on_pattern = music_ticks_on_pattern;
    return s;
}

void AudioEngine::setState(const AudioStateSnapshot& s) {
    for(int i=0; i<4; i++) channels[i] = s.channels[i];
    music_pattern = s.music_pattern;
    music_tick_timer = s.music_tick_timer;
    music_speed = s.music_speed;
    music_loop_start = s.music_loop_start;
    music_mask = s.music_mask;
    music_playing = s.music_playing;
    music_patterns_played = s.music_patterns_played;
    music_ticks_on_pattern = s.music_ticks_on_pattern;
}

static const AudioEngine::MixerTickSnap* get_last_snap(const AudioEngine &audio) {
    if (!audio.snaps_ready) return nullptr;
    int idx = (audio.snap_w - 1) & (AudioEngine::SNAP_COUNT - 1);
    return &audio.snaps[idx];
}

int AudioEngine::get_sfx_id_hp(int ch) const {
    if (ch < 0 || ch >= CHANNELS) return -1;
    const MixerTickSnap *snap = get_last_snap(*this);
    return snap ? snap->sfx_id[ch] : get_sfx_id(ch);
}

int AudioEngine::get_note_row_hp(int ch) const {
    if (ch < 0 || ch >= CHANNELS) return -1;
    const MixerTickSnap *snap = get_last_snap(*this);
    return snap ? snap->note_row[ch] : get_note_row(ch);
}

int AudioEngine::get_music_pattern_hp() const {
    const MixerTickSnap *snap = get_last_snap(*this);
    return snap ? snap->music_pattern : get_music_pattern();
}

int AudioEngine::get_music_patterns_played_hp() const {
    const MixerTickSnap *snap = get_last_snap(*this);
    return snap ? snap->patterns_played : get_music_patterns_played();
}

int AudioEngine::get_music_ticks_on_pattern_hp() const {
    const MixerTickSnap *snap = get_last_snap(*this);
    return snap ? snap->ticks_on_pattern : get_music_ticks_on_pattern();
}

bool AudioEngine::is_music_playing_hp() const {
    const MixerTickSnap *snap = get_last_snap(*this);
    return snap ? snap->music_playing : is_music_playing();
}
