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

enum KickModel : int
{
    kKick909 = 0,      /* sine with a pitch sweep, bright beater click        */
    kKick808,          /* gentler drop, longer boom, soft low thump           */
    kKickFm,           /* two-operator FM: a metallic attack over a sine body */
    kKickModelCount
};

enum SnareModel : int
{
    kSnare909 = 0,     /* two shell tones with a pitch drop, plus snares      */
    kSnare808,         /* ringing tonal body, brighter, shorter snares        */
    kSnareClap,        /* three quick noise bursts and a tail                 */
    kSnareModelCount
};

struct KickParams
{
    int   model   = kKick909;
    float tune_hz = 52.0f;    /* resting pitch of the body                    */
    float level   = 0.75f;    /* 0..1 (squared inside)                        */
    float decay_s = 0.45f;    /* time for the body to fall 60 dB              */
    float bend    = 0.45f;    /* 0..1 how far the pitch drops                 */
    float sweep   = 0.50f;    /* 0..1 how fast: 0 snaps down (808), 1 glides (909) */
    float punch   = 0.50f;    /* 0..1 front louder than the tail, plus a knock */
    float click   = 0.30f;    /* 0..1 level of the beater click               */
    float drive   = 0.25f;    /* 0..1 saturation                              */
};

struct SnareParams
{
    int   model   = kSnare909;
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
    float kick_amt  = 1.00f;   /* 0..1 how much a kick trigger ducks (fixed, no knob) */
    float snare_amt = 0.00f;   /* 0..1 how much a snare trigger ducks (fixed, no knob) */
};

struct ReverbParams
{
    float amount = 0.0f;      /* 0..1: more is louder and longer (0.3 s .. 3.5 s) */
    bool  kick   = false;     /* false: snare only. true: the kick is sent too  */
};

/** Techno rumble: the kick through a dark reverb, driven, low-passed and
 *  pumped by the kick's own duck envelope so it swells between hits. */
struct RumbleParams
{
    bool  on      = true;     /* performance on/off; off lets the tail ring out */
    float amount  = 0.0f;     /* 0..1 level                                   */
    float tone_hz = 140.0f;   /* low-pass on the rumble, 50 .. 400 Hz         */
    float decay_s = 1.6f;     /* reverb decay (60 dB), 0.4 .. 4 s             */
    float drive   = 0.5f;     /* 0..1 saturation                              */
};

struct Params
{
    KickParams   kick;
    SnareParams  snare;
    DuckParams   duck;
    ReverbParams reverb;
    RumbleParams rumble;
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
 * Kick voice. Three models share the same controls (see KickParams); each
 * reads them its own way. A model change takes effect on the next hit.
 */
class Kick
{
  public:
    void  Init(float sr);
    void  Set(const KickParams& p);
    void  Trigger(float vel);
    float Tick();
    float Env() const { return amp_; }
    int   Model() const { return model_; }

  private:
    void Recompute();

    float      sr_ = 48000.0f;
    Noise      noise_;
    KickParams p_;
    int        model_ = kKick909;     /* sounding model (latched on a hit) */

    /* state */
    float phase_ = 0.0f, amp_ = 0.0f, mph_ = 0.0f;
    float env_bend_ = 0.0f, env_punch_ = 0.0f, env_knock_ = 0.0f, env_click_ = 0.0f, env_idx_ = 0.0f;
    float click_hp_ = 0.0f, click_prev_ = 0.0f, click_ph_ = 0.0f;
    float gain_ = 0.0f, gain_target_ = 0.0f, vel_ = 1.0f;
    float declick_ = 0.0f, last_ = 0.0f;

    /* coefficients */
    float inc_ = 0.0f, amp_coef_ = 0.0f;
    float bend_amt_ = 0.0f, bend_coef_ = 0.0f, bend_sq_ = 0.5f, bend_rt_ = 0.5f;  /* curve mix */
    float punch_pitch_ = 0.0f, punch_depth_ = 0.0f, punch_coef_ = 0.0f, knock_coef_ = 0.0f;
    float click_gain_ = 0.0f, click_coef_ = 0.0f, click_hp_a_ = 0.0f, click_inc_ = 0.0f;
    float thump_coef_ = 0.0f;
    float fm_ratio_ = 1.0f, fm_index_ = 0.0f, idx_coef_ = 0.0f;
    float drive_g_ = 1.0f, drive_norm_ = 1.0f;
    float level_ = 0.0f, declick_coef_ = 0.0f;
};

/**
 * Snare voice. Three models share the same controls (see SnareParams); a
 * model change takes effect on the next hit.
 */
class Snare
{
  public:
    void  Init(float sr);
    void  Set(const SnareParams& p);
    void  Trigger(float vel);
    float Tick();
    float Env() const
    {
        float e = e_noise_ > e1_ ? e_noise_ : e1_;
        return e_burst_ > e ? e_burst_ : e;
    }
    int   Model() const { return model_; }

  private:
    void Recompute();

    float       sr_ = 48000.0f;
    Noise       noise_;
    SnareParams p_;
    int         model_ = kSnare909;

    /* state */
    float p1_ = 0.0f, p2_ = 0.0f;
    float e1_ = 0.0f, e2_ = 0.0f, e_noise_ = 0.0f, e_pitch_ = 0.0f, e_burst_ = 0.0f;
    float lp1_ = 0.0f, lp2_ = 0.0f, hp_ = 0.0f;
    float gain_ = 0.0f, gain_target_ = 0.0f, vel_ = 1.0f;
    float declick_ = 0.0f, last_ = 0.0f;
    uint32_t clap_t_ = 0, clap_next_ = 0;   /* samples since the hit, next burst */
    uint8_t  bursts_left_ = 0;
    Svf      bp_;

    /* coefficients */
    float inc1_ = 0.0f, inc2_ = 0.0f;
    float c1_ = 0.0f, c2_ = 0.0f, c_noise_ = 0.0f, c_pitch_ = 0.0f, c_burst_ = 0.0f;
    float pitch_amt_ = 0.0f;
    float lp_a_ = 0.5f, hp_a_ = 0.05f;
    float body_gain_ = 1.0f, noise_gain_ = 0.0f, tail_gain_ = 0.0f;
    uint32_t clap_spacing_ = 480;
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

/**
 * Rumble: the kick into a small, dark reverb, then drive, then a low-pass.
 * Everything that matters is below a few hundred hertz, so the reverb and
 * filters run at a quarter of the sample rate (12 kHz), which makes the
 * whole thing cheap. Mono: sub should not wander.
 */
class Rumble
{
  public:
    static constexpr int kDecim   = 4;
    static constexpr int kLines   = 4;
    static constexpr int kMaxLine = 2048;     /* 170 ms at 12 kHz */
    static constexpr int kNumAp   = 2;
    static constexpr int kMaxAp   = 256;
    static constexpr size_t kMemFloats = kLines * kMaxLine + kNumAp * kMaxAp;

    void  Init(float sr, float* mem);
    void  Set(const RumbleParams& p);
    /** One full-rate sample: @p in is the kick, @p pump the gain the kick's
     *  duck envelope allows (1 = open). Returns the rumble. */
    float Tick(float in, float pump);

  private:
    void LowRate(float x);

    float  sr_ = 48000.0f, srl_ = 12000.0f;
    float* line_[kLines] = {};
    int    len_[kLines] = {}, w_ = 0;
    float  g_[kLines] = {}, damp_[kLines] = {}, damp_a_ = 0.5f;
    float* ap_[kNumAp] = {};
    int    ap_len_[kNumAp] = {}, ap_pos_[kNumAp] = {};
    Svf    pre_[2], tone_[2], post_;
    int    phase_ = 0;
    float  prev_ = 0.0f, cur_ = 0.0f;           /* low-rate output, for interpolation */
    float  send_ = 0.0f, send_target_ = 1.0f;
    float  amount_ = 0.0f, amount_target_ = 0.0f;
    float  drive_pre_ = 1.0f, drive_post_ = 1.0f;
    float  dc_x_ = 0.0f, dc_y_ = 0.0f, dc_a_ = 0.997f;
};

/* ── Engine ────────────────────────────────────────────────────────────── */

class Engine
{
  public:
    /** Delay memory the engine needs, in floats (the reverb and the
     *  rumble). Kept out of the engine so the firmware can place it in
     *  external RAM. */
    static constexpr size_t kMemFloats = Reverb::kMemFloats + Rumble::kMemFloats;

    /** @p mem: kMemFloats floats; may be uninitialised. */
    void Init(float sample_rate, float* mem);

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
    float RumbleLevel() const { return rumble_out_; }   /* |rumble|, smoothed */

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
    Rumble  rumble_;
    DuckEnv env_k_, env_s_;
    float   pump_ = 1.0f, rumble_out_ = 0.0f;

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
