/**
 * @file blas_dsp.h
 * @brief Blas audio engine: trigger-driven ducker + 909-style kick and snare.
 *
 * Pure DSP. Knows nothing about the Alchemy SDK, knobs or jacks, so it
 * builds and runs on a host compiler (see tests/host_test.cpp).
 *
 * Signal flow (stereo):
 *
 *   in ──► ducker (Volume | Split | Carve) ──►(+)──► soft clip ──► out
 *                                              ▲
 *       kick + snare (mono) ──┬────────────────┤
 *                             └─► reverb (stereo, snare or both)
 *
 * The drums and their reverb are added after the ducker, so they are
 * never ducked.
 * Each drum trigger also fires that drum's own duck envelope.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace blas {

/* ── Parameters (engineering units; the surface owns the knob curves) ──── */

enum DuckType : int
{
    kDuckVolume = 0,   /* the whole signal dips                               */
    kDuckSplit  = 1,   /* a band you place with Focus (centre) and Width (Q) dips */
    kDuckCarve  = 2,   /* a dynamic EQ cut around each drum's own tuning      */
    kDuckTypeCount
};

struct KickParams
{
    float tune_hz = 52.0f;    /* resting pitch of the body                    */
    float level   = 0.75f;    /* 0..1 (squared inside)                        */
    float decay_s = 0.45f;    /* time for the body to fall 60 dB              */
    float bend    = 0.45f;    /* 0..1 depth (and length) of the pitch drop    */
    float punch   = 0.50f;    /* 0..1 fast knock: pitch spike + level bump    */
    float click   = 0.30f;    /* 0..1 level of the click transient            */
    float drive   = 0.25f;    /* 0..1 saturation                              */
};

struct SnareParams
{
    float tune_hz = 185.0f;   /* lower shell oscillator                       */
    float level   = 0.60f;    /* 0..1 (squared inside)                        */
    float decay_s = 0.22f;    /* shell decay; the snares ring a little longer */
    float snappy  = 0.60f;    /* 0..1 amount of snare noise                   */
    float tone    = 0.50f;    /* 0..1 brightness of the snare noise           */
    float bend    = 0.30f;    /* 0..1 pitch drop on the shell                 */
    float drive   = 0.20f;    /* 0..1 saturation                              */
};

struct DuckParams
{
    float depth     = 0.60f;   /* 0..1; 1 = full mute at the bottom           */
    float attack_s  = 0.002f;  /* time to reach full ducking                  */
    float hold_s    = 0.020f;  /* time held at full ducking                   */
    float release_s = 0.220f;  /* recovery time                               */
    float curve     = 0.60f;   /* 0..1 recovery shape: 0 late, .5 linear, 1 early */
    int   type      = kDuckVolume;
    float focus     = 0.50f;   /* Split: band centre. Carve: shift around tuning */
    float width     = 0.60f;   /* Split and Carve: width of the cut (0 narrow, 1 wide) */
    float kick_amt  = 1.00f;   /* 0..1 how much a kick trigger ducks          */
    float snare_amt = 0.00f;   /* 0..1 how much a snare trigger ducks (fixed, no knob) */
};

struct ReverbParams
{
    float amount = 0.0f;      /* 0..1: more is louder and longer (0.3 s .. 3.5 s) */
    bool  kick   = false;     /* false: snare only. true: the kick is sent too  */
};

struct Params
{
    KickParams   kick;
    SnareParams  snare;
    DuckParams   duck;
    ReverbParams reverb;
};

/* ── Building blocks ───────────────────────────────────────────────────── */

class Noise
{
  public:
    float Next()
    {
        s_ = s_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(s_)) * (1.0f / 2147483648.0f);
    }

  private:
    uint32_t s_ = 0x1234567u;
};

class Kick
{
  public:
    void  Init(float sr);
    void  Set(const KickParams& p);
    void  Trigger(float vel);
    float Tick();
    float Env() const { return amp_; }

  private:
    float sr_ = 48000.0f;
    Noise noise_;

    /* state */
    float phase_ = 0.0f, amp_ = 0.0f;
    float env_bend_ = 0.0f, env_punch_ = 0.0f, env_click_ = 0.0f;
    float click_lp_ = 0.0f;
    float gain_ = 0.0f, gain_target_ = 0.0f, vel_ = 1.0f;
    float declick_ = 0.0f, last_ = 0.0f;

    /* coefficients */
    float inc_ = 0.0f, amp_coef_ = 0.0f;
    float bend_amt_ = 0.0f, bend_coef_ = 0.0f;
    float punch_pitch_ = 0.0f, punch_amp_ = 0.0f, punch_coef_ = 0.0f;
    float click_gain_ = 0.0f, click_coef_ = 0.0f;
    float drive_g_ = 1.0f, drive_norm_ = 1.0f;
    float level_ = 0.0f, declick_coef_ = 0.0f;
};

class Snare
{
  public:
    void  Init(float sr);
    void  Set(const SnareParams& p);
    void  Trigger(float vel);
    float Tick();
    float Env() const { return e_noise_ > e1_ ? e_noise_ : e1_; }

  private:
    float sr_ = 48000.0f;
    Noise noise_;

    /* state */
    float p1_ = 0.0f, p2_ = 0.0f;
    float e1_ = 0.0f, e2_ = 0.0f, e_noise_ = 0.0f, e_pitch_ = 0.0f;
    float lp1_ = 0.0f, lp2_ = 0.0f, hp_ = 0.0f;
    float gain_ = 0.0f, gain_target_ = 0.0f, vel_ = 1.0f;
    float declick_ = 0.0f, last_ = 0.0f;

    /* coefficients */
    float inc1_ = 0.0f, inc2_ = 0.0f;
    float c1_ = 0.0f, c2_ = 0.0f, c_noise_ = 0.0f, c_pitch_ = 0.0f;
    float pitch_amt_ = 0.0f;
    float lp_a_ = 0.5f, hp_a_ = 0.05f;
    float body_gain_ = 1.0f, noise_gain_ = 0.0f;
    float drive_g_ = 1.0f, drive_norm_ = 1.0f;
    float level_ = 0.0f, declick_coef_ = 0.0f;
};

/** Attack / hold / release envelope, 0 (no ducking) .. 1 (full ducking). */
class DuckEnv
{
  public:
    void  Trigger() { state_ = kAttack; start_ = v_; t_ = 0.0f; }
    /** Advance by @p dt seconds. @p shape_k is the release exponent. */
    float Advance(float dt, float attack_s, float hold_s, float release_s, float shape_k);
    float Value() const { return v_; }
    void  Reset() { state_ = kIdle; v_ = 0.0f; t_ = 0.0f; start_ = 0.0f; }

  private:
    enum State : uint8_t { kIdle, kAttack, kHold, kRelease };
    State state_ = kIdle;
    float v_ = 0.0f, t_ = 0.0f, start_ = 0.0f;
};

/** Trapezoidal state-variable filter (Simper). Stable under modulation. */
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float g = 0.1f, k = 1.4142f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

    void SetG(float g_in) { g = g_in; }
    void SetK(float k_in)
    {
        k  = k_in;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void Tick(float v0, float& v1, float& v2)
    {
        const float v3 = v0 - ic2;
        v1  = a1 * ic1 + a2 * v3;
        v2  = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
    }
    void Reset() { ic1 = ic2 = 0.0f; }
};

/**
 * Room reverb for the drums: mono in, stereo out, wet only.
 *
 * Four all-pass diffusers feed an eight-line feedback network (Hadamard
 * mixing, 30..80 ms lines). Dense from the first few milliseconds, darker
 * as it decays, and two of the lines drift slowly so no single resonance
 * can ring: a room rather than a spring or a tin can.
 *
 * One control. Turning it up raises the level and lengthens the decay
 * together, from a small tight room (0.3 s) to a hall (3.5 s).
 */
class Reverb
{
  public:
    static constexpr int kLines   = 8;
    static constexpr int kMaxLine = 4096;    /* 85 ms at 48 kHz; power of two */
    static constexpr int kNumAp   = 4;
    static constexpr int kMaxAp   = 1024;
    /** Floats of delay memory the caller must provide to Init(). */
    static constexpr size_t kMemFloats = kLines * kMaxLine + kNumAp * kMaxAp;

    /** @p mem holds kMemFloats floats and outlives the reverb. It may be
     *  uninitialised (e.g. external SDRAM); Init clears it. */
    void Init(float sr, float* mem);
    void Set(float amount);
    void Tick(float in, float& out_l, float& out_r);

    /** Decay time (60 dB) for a given amount, in seconds. */
    static float DecaySeconds(float amount);

  private:
    float  sr_ = 48000.0f;
    float* line_[kLines] = {};
    int    len_[kLines] = {};
    int    w_ = 0;                            /* shared write index */
    float  lp_[kLines] = {}, g_[kLines] = {};
    float* ap_[kNumAp] = {};
    int    ap_len_[kNumAp] = {}, ap_pos_[kNumAp] = {};
    float  hp_ = 0.0f, hp_a_ = 0.0f, in_lp_ = 0.0f, in_lp_a_ = 1.0f, damp_a_ = 0.5f;
    float  lfo_ = 0.0f, lfo_inc_ = 0.0f, mod_depth_ = 0.0f;
    float  wet_ = 0.0f, wet_target_ = 0.0f;
};

/* ── Engine ────────────────────────────────────────────────────────────── */

class Engine
{
  public:
    /** @p reverb_mem: Reverb::kMemFloats floats of delay memory (kept out
     *  of the engine so the firmware can place it in external RAM). */
    void Init(float sample_rate, float* reverb_mem);

    /** Latest parameters. Call from the audio thread, once per block. */
    void Set(const Params& p);

    /** Fire a drum (and its duck envelope). @p vel 0..1. Audio thread. */
    void TriggerKick(float vel);
    void TriggerSnare(float vel);

    /** Process @p n frames. In and out may be the same buffers. */
    void Process(const float* in_l, const float* in_r,
                 float* out_l, float* out_r, size_t n);

    /* Meters, valid after Process (read from any thread). */
    float DuckGain()  const { return gain_; }      /* 1 = open, 0 = fully ducked     */
    float DuckShape() const { return shape_; }     /* 0..1 envelope, depth-independent */
    float KickEnv()   const { return kick_.Env(); }
    float SnareEnv()  const { return snare_.Env(); }

    /** Centre frequencies in use (for tests and displays). */
    float SplitHz()      const { return split_hz_; }
    float KickCarveHz()  const { return carve_k_hz_; }
    float SnareCarveHz() const { return carve_s_hz_; }

  private:
    void UpdateFilters();

    float  sr_ = 48000.0f;
    Params p_;

    Kick    kick_;
    Snare   snare_;
    Reverb  reverb_;
    DuckEnv env_k_, env_s_;

    /* Split: one band-pass per channel, subtracted by the ducking amount. */
    Svf split_[2];
    /* Carve: one bell per drum, per channel. */
    Svf bell_k_[2], bell_s_[2];
    float bell_k_m1_ = 0.0f, bell_s_m1_ = 0.0f;

    float split_hz_ = 0.0f, carve_k_hz_ = 0.0f, carve_s_hz_ = 0.0f, carve_q_ = 0.8f;
    float split_g_ = 0.0f, carve_k_g_ = 0.0f, carve_s_g_ = 0.0f;

    float gain_ = 1.0f;                     /* current full-band gain       */
    float shape_ = 0.0f;
    float w_[kDuckTypeCount] = {1.0f, 0.0f, 0.0f};   /* type crossfade     */
};

} // namespace blas
