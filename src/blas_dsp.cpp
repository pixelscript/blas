/**
 * @file blas_dsp.cpp
 * @brief Blas audio engine. See blas_dsp.h.
 */
#include "blas_dsp.h"

#include <cmath>

namespace blas {

namespace {

constexpr float kPi = 3.14159265358979f;

inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

/** sin(2*pi*p) for p in [0, 1). Parabolic with one refinement, ~0.1% error. */
inline float Sin01(float p)
{
    const float u = 2.0f * p - 1.0f;
    float y = 4.0f * u * (1.0f - std::fabs(u));
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return -y;
}

/** Rational tanh, exact limits at +-3. */
inline float FastTanh(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Per-sample multiplier for an exponential that falls 60 dB in @p t60 s. */
inline float Coef60(float t60, float sr) { return std::exp(-6.9078f / (t60 * sr)); }

/** Per-sample multiplier for a time constant of @p tau seconds. */
inline float CoefTau(float tau, float sr) { return std::exp(-1.0f / (tau * sr)); }

/** Transparent below 0.7, then bends smoothly towards 1.0. */
inline float SoftClip(float x)
{
    const float a = std::fabs(x);
    if (a <= 0.7f) return x;
    const float y = 0.7f + 0.3f * FastTanh((a - 0.7f) * (1.0f / 0.3f));
    return x < 0.0f ? -y : y;
}

constexpr float kSilence = 1.0e-5f;

} // namespace

/* ═════════════════════════════════════════════════════════════════════════
 *  Kick
 * ═════════════════════════════════════════════════════════════════════════ */

void Kick::Init(float sr)
{
    sr_           = sr;
    punch_coef_   = CoefTau(0.030f, sr);     /* the loud front: ~30 ms     */
    knock_coef_   = CoefTau(0.007f, sr);     /* the pitch knock: ~7 ms     */
    click_coef_   = CoefTau(0.0035f, sr);    /* the click: ~3.5 ms         */
    thump_coef_   = CoefTau(0.0025f, sr);    /* 808 thump: ~2.5 ms         */
    idx_coef_     = CoefTau(0.015f, sr);     /* FM brightness: ~15 ms      */
    click_hp_a_   = std::exp(-2.0f * kPi * 1200.0f / sr);   /* noise above 1.2 kHz */
    click_inc_    = 2100.0f / sr;            /* the beater's ping          */
    declick_coef_ = CoefTau(0.0015f, sr);
    model_        = kKick909;
    Set(KickParams{});
}

void Kick::Set(const KickParams& p)
{
    p_ = p;
    Recompute();
}

void Kick::Recompute()
{
    const KickParams& p = p_;
    const float tune  = Clamp(p.tune_hz, 20.0f, 400.0f);
    float       decay = Clamp(p.decay_s, 0.03f, 5.0f);
    const float bend  = Clamp(p.bend, 0.0f, 1.0f);
    const float punch = Clamp(p.punch, 0.0f, 1.0f);
    const float drive = Clamp(p.drive, 0.0f, 1.0f);
    const float level = Clamp(p.level, 0.0f, 1.0f);
    const float click = Clamp(p.click, 0.0f, 1.0f);
    const float sweep = Clamp(p.sweep, 0.0f, 1.0f);

    /* Bend sets how far the pitch drops; Sweep how it gets back down.
     * Sweep turns two things together: the time constant (5 ms snap to
     * 90 ms glide, 21 ms at noon) and the curve. Left the drop is squared,
     * reaching the tuned pitch early like an 808; right it is e*(2-e),
     * hanging high before it falls, like a 909. At noon the two average to
     * a plain exponential. */
    float bend_depth = 6.0f;
    bend_coef_ = CoefTau(0.005f * std::pow(18.0f, sweep), sr_);
    bend_sq_   = 1.0f - sweep;
    bend_rt_   = sweep;

    /* Punch is a transient shaper on the body: the first ~30 ms stay at
     * full level while everything after drops, by up to 9 dB, so the
     * front stands out against the tail. A short pitch knock (up to two
     * octaves, ~7 ms) gives it the hit. */
    punch_pitch_ = punch * 3.0f;
    punch_depth_ = punch * 0.65f;

    click_gain_ = click * 0.9f;
    float drive_amt = drive * drive * 14.0f;
    fm_ratio_ = 1.0f;
    fm_index_ = 0.0f;

    switch (model_)
    {
        case kKick808:
            /* A resonant boom: the pitch only drops a little, the tail is
             * half as long again, the click is a soft low thump and the
             * drive is gentler. */
            bend_depth   = 2.0f;
            decay       *= 1.5f;
            punch_pitch_ = punch * 1.0f;
            click_gain_  = click * 0.8f;
            drive_amt    = drive * drive * 8.0f;
            break;

        case kKickFm:
            /* Two operators. Click sets how bright the attack is (the
             * modulation index, 1.5 to 9.5 radians, fading over ~15 ms), Punch
             * the modulator's ratio: 1 is warm, 2 hollow, in between clangs.
             * Even at zero Click there is a little FM edge (1.5 radians).
             * The body underneath is the same swept sine. */
            fm_index_    = 1.5f + click * 8.0f;
            fm_ratio_    = 1.0f + punch * 3.0f;
            punch_pitch_ = 0.0f;
            punch_depth_ = 0.0f;
            click_gain_  = 0.0f;
            break;

        default:
            break;
    }

    inc_       = tune / sr_;
    amp_coef_  = Coef60(Clamp(decay, 0.03f, 7.5f), sr_);
    bend_amt_  = bend * bend * bend_depth;
    drive_g_    = 1.0f + drive_amt;
    drive_norm_ = 1.0f / FastTanh(drive_g_);

    level_       = level * level;
    gain_target_ = level_ * vel_;
}

void Kick::Trigger(float vel)
{
    /* A model change waits for a hit, so a tail never changes voice. */
    const int want = (p_.model >= 0 && p_.model < kKickModelCount) ? p_.model : kKick909;
    if (want != model_) { model_ = want; Recompute(); }

    vel_         = Clamp(vel, 0.0f, 1.0f);
    gain_target_ = level_ * vel_;
    gain_        = gain_target_;
    /* Restart the body from zero phase so every hit lands the same, and
     * carry the old tail's last value out as a short decaying offset so
     * the restart cannot click. */
    declick_   = last_;
    phase_     = 0.0f;
    mph_       = 0.0f;
    amp_       = 1.0f;
    env_bend_  = 1.0f;
    env_punch_ = 1.0f;
    env_knock_ = 1.0f;
    env_click_ = 1.0f;
    env_idx_   = 1.0f;
    click_hp_  = 0.0f;
    click_prev_ = 0.0f;
    click_ph_  = 0.0f;
}

float Kick::Tick()
{
    if (amp_ < kSilence && env_click_ < kSilence
        && declick_ < kSilence && declick_ > -kSilence)
    {
        amp_ = env_click_ = env_bend_ = env_punch_ = env_knock_ = env_idx_ = 0.0f;
        declick_ = 0.0f;
        last_    = 0.0f;
        return 0.0f;
    }

    gain_ += 0.002f * (gain_target_ - gain_);

    const float bend_env = bend_sq_ * env_bend_ * env_bend_
                         + bend_rt_ * env_bend_ * (2.0f - env_bend_);
    const float pitch    = 1.0f + bend_amt_ * bend_env + punch_pitch_ * env_knock_;
    phase_ += inc_ * pitch;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    if (phase_ >= 1.0f) phase_ = 0.0f;

    float osc;
    if (model_ == kKickFm)
    {
        mph_ += inc_ * pitch * fm_ratio_;
        while (mph_ >= 1.0f) mph_ -= 1.0f;
        /* Phase modulation, index in radians -> cycles. */
        float ph = phase_ + fm_index_ * env_idx_ * (1.0f / (2.0f * kPi)) * Sin01(mph_);
        ph -= std::floor(ph);
        osc = Sin01(ph);
    }
    else
    {
        osc = Sin01(phase_);
    }

    const float shaper = 1.0f - punch_depth_ * (1.0f - env_punch_);
    float body = osc * amp_ * shaper;
    body = FastTanh(body * drive_g_) * drive_norm_;

    float click = 0.0f;
    if (env_click_ > kSilence && click_gain_ > 0.0f)
    {
        if (model_ == kKick808)
        {
            /* A soft, low thump: a one-sided pulse, no top end. */
            click = env_click_;
        }
        else
        {
            /* Click: a beater hitting the skin. High-passed noise plus a
             * short 2.1 kHz ping, both gone in a few milliseconds. */
            const float n = noise_.Next();
            click_hp_  = click_hp_a_ * (click_hp_ + n - click_prev_);
            click_prev_ = n;
            click_ph_ += click_inc_;
            if (click_ph_ >= 1.0f) click_ph_ -= 1.0f;
            click = (0.6f * click_hp_ + 0.7f * Sin01(click_ph_)) * env_click_;
        }
    }

    amp_       *= amp_coef_;
    env_bend_  *= bend_coef_;
    env_punch_ *= punch_coef_;
    env_knock_ *= knock_coef_;
    env_click_ *= (model_ == kKick808) ? thump_coef_ : click_coef_;
    env_idx_   *= idx_coef_;

    float out = (body + click * click_gain_) * gain_;
    out      += declick_;
    declick_ *= declick_coef_;
    last_     = out;
    return out;
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Snare
 * ═════════════════════════════════════════════════════════════════════════ */

void Snare::Init(float sr)
{
    sr_           = sr;
    c_pitch_      = CoefTau(0.012f, sr);
    c_burst_      = CoefTau(0.0025f, sr);   /* each clap burst: ~2.5 ms */
    declick_coef_ = CoefTau(0.0015f, sr);
    model_        = kSnare909;
    bp_.Reset();
    Set(SnareParams{});
}

void Snare::Set(const SnareParams& p)
{
    p_ = p;
    Recompute();
}

void Snare::Recompute()
{
    const SnareParams& p = p_;
    const float tune   = Clamp(p.tune_hz, 60.0f, 800.0f);
    const float decay  = Clamp(p.decay_s, 0.03f, 3.0f);
    const float snappy = Clamp(p.snappy, 0.0f, 1.0f);
    const float tone   = Clamp(p.tone, 0.0f, 1.0f);
    const float drive  = Clamp(p.drive, 0.0f, 1.0f);
    const float level  = Clamp(p.level, 0.0f, 1.0f);
    const float bend   = Clamp(p.bend, 0.0f, 1.0f);

    float ratio = 1.59f, lp_hz, hp_hz;
    switch (model_)
    {
        case kSnare808:
            /* A ringing, tonal body (two modes nearer a fifth apart, barely
             * any pitch drop) and brighter, shorter snares. */
            ratio      = 1.47f;
            pitch_amt_ = bend * 0.3f;
            c1_        = Coef60(decay * 1.3f, sr_);
            c2_        = Coef60(decay * 0.9f, sr_);
            c_noise_   = Coef60(decay * 0.8f, sr_);
            lp_hz      = 6000.0f * std::pow(2.3f, tone);
            hp_hz      = 1500.0f + 2500.0f * tone;
            noise_gain_ = snappy * 2.6f;
            body_gain_  = 1.0f - 0.15f * snappy;
            break;

        case kSnareClap:
        {
            /* Three quick noise bursts and a tail, through a band-pass.
             * Tune places the band (5x: 550 Hz .. 1.65 kHz), Tone opens it
             * up, Bend spreads the bursts (6 .. 16 ms apart), Snappy sets
             * the tail against the bursts and Decay its length. */
            const float centre = Clamp(tune * 5.0f * (0.7f + 0.8f * tone), 200.0f, 0.45f * sr_);
            bp_.SetG(std::tan(kPi * centre / sr_));
            bp_.SetK(1.0f / (1.2f + 1.6f * tone));
            clap_spacing_ = static_cast<uint32_t>((0.006f + 0.010f * bend) * sr_);
            c_noise_      = Coef60(decay * 1.2f, sr_);
            tail_gain_    = 0.35f + 0.65f * snappy;
            noise_gain_   = 5.5f;
            pitch_amt_    = 0.0f;
            lp_hz = hp_hz = 1000.0f;
            break;
        }

        default:
            pitch_amt_ = bend * 1.2f;
            c1_        = Coef60(decay, sr_);
            c2_        = Coef60(decay * 0.6f, sr_);
            c_noise_   = Coef60(decay * 1.4f, sr_);
            /* Tone: the noise low-pass opens from 2.5 kHz to 11 kHz while
             * the high-pass rises, so bright settings are thinner as well
             * as brighter. */
            lp_hz      = 2500.0f * std::pow(4.4f, tone);
            hp_hz      = 250.0f + 1200.0f * tone;
            noise_gain_ = snappy * 2.2f;
            body_gain_  = 1.0f - 0.35f * snappy;
            break;
    }

    inc1_ = tune / sr_;
    inc2_ = inc1_ * ratio;
    lp_a_ = 1.0f - std::exp(-2.0f * kPi * Clamp(lp_hz, 20.0f, 0.45f * sr_) / sr_);
    hp_a_ = 1.0f - std::exp(-2.0f * kPi * Clamp(hp_hz, 20.0f, 0.45f * sr_) / sr_);

    drive_g_    = 1.0f + drive * drive * 10.0f;
    drive_norm_ = 1.0f / FastTanh(drive_g_);

    level_       = level * level;
    gain_target_ = level_ * vel_;
}

void Snare::Trigger(float vel)
{
    const int want = (p_.model >= 0 && p_.model < kSnareModelCount) ? p_.model : kSnare909;
    if (want != model_) { model_ = want; Recompute(); }

    vel_         = Clamp(vel, 0.0f, 1.0f);
    gain_target_ = level_ * vel_;
    gain_        = gain_target_;
    declick_     = last_;
    p1_ = p2_ = 0.0f;
    if (model_ == kSnareClap)
    {
        e1_ = e2_ = e_pitch_ = e_noise_ = 0.0f;
        e_burst_     = 1.0f;
        clap_t_      = 0;
        clap_next_   = clap_spacing_;
        bursts_left_ = 3;              /* two more bursts, then the tail */
    }
    else
    {
        e1_ = e2_ = e_noise_ = e_pitch_ = 1.0f;
        e_burst_     = 0.0f;
        bursts_left_ = 0;
    }
}

float Snare::Tick()
{
    if (e1_ < kSilence && e_noise_ < kSilence && e_burst_ < kSilence && bursts_left_ == 0
        && declick_ < kSilence && declick_ > -kSilence)
    {
        e1_ = e2_ = e_noise_ = e_pitch_ = e_burst_ = 0.0f;
        declick_ = 0.0f;
        last_    = 0.0f;
        return 0.0f;
    }

    gain_ += 0.002f * (gain_target_ - gain_);

    float x;
    if (model_ == kSnareClap)
    {
        if (bursts_left_ > 0 && ++clap_t_ >= clap_next_)
        {
            if (--bursts_left_ > 0) { e_burst_ = 1.0f; clap_next_ += clap_spacing_; }
            else                    { e_noise_ = 1.0f; }
        }
        float v1, v2;
        bp_.Tick(noise_.Next(), v1, v2);
        const float band = v1 * bp_.k;                 /* unity at centre */
        x = band * (e_burst_ + tail_gain_ * e_noise_) * noise_gain_;
        e_burst_ *= c_burst_;
        e_noise_ *= c_noise_;
    }
    else
    {
        const float pitch = 1.0f + pitch_amt_ * e_pitch_;
        p1_ += inc1_ * pitch;
        p2_ += inc2_ * pitch;
        if (p1_ >= 1.0f) p1_ -= 1.0f;
        if (p2_ >= 1.0f) p2_ -= 1.0f;
        if (p1_ >= 1.0f) p1_ = 0.0f;
        if (p2_ >= 1.0f) p2_ = 0.0f;

        const float body = (Sin01(p1_) * e1_ + 0.55f * Sin01(p2_) * e2_) * body_gain_;

        const float n = noise_.Next();
        lp1_ += lp_a_ * (n - lp1_);
        lp2_ += lp_a_ * (lp1_ - lp2_);
        hp_  += hp_a_ * (lp2_ - hp_);
        const float snares = (lp2_ - hp_) * e_noise_ * noise_gain_;

        x = body * 0.8f + snares;

        e1_      *= c1_;
        e2_      *= c2_;
        e_noise_ *= c_noise_;
        e_pitch_ *= c_pitch_;
    }
    x = FastTanh(x * drive_g_) * drive_norm_;

    float out = x * gain_;
    out      += declick_;
    declick_ *= declick_coef_;
    last_     = out;
    return out;
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Reverb
 * ═════════════════════════════════════════════════════════════════════════ */

void Reverb::Init(float sr, float* mem)
{
    sr_ = sr;
    /* Mutually prime line lengths, 30 .. 80 ms at 48 kHz: long enough that
     * their modes are closely spaced (short lines are what ring). */
    static const int kLineLen[kLines] = {1423, 1669, 1933, 2251, 2647, 2969, 3371, 3823};
    static const int kApLen[kNumAp]   = {173, 229, 449, 613};
    const float scale = sr / 48000.0f;

    for (int i = 0; i < kLines; i++)
    {
        line_[i] = mem + i * kMaxLine;
        int n = static_cast<int>(static_cast<float>(kLineLen[i]) * scale);
        len_[i] = n < 16 ? 16 : (n > kMaxLine - 64 ? kMaxLine - 64 : n);
        lp_[i]  = 0.0f;
        for (int k = 0; k < kMaxLine; k++) line_[i][k] = 0.0f;
    }
    for (int i = 0; i < kNumAp; i++)
    {
        ap_[i] = mem + kLines * kMaxLine + i * kMaxAp;
        int n = static_cast<int>(static_cast<float>(kApLen[i]) * scale);
        ap_len_[i] = n < 4 ? 4 : (n > kMaxAp ? kMaxAp : n);
        ap_pos_[i] = 0;
        for (int k = 0; k < kMaxAp; k++) ap_[i][k] = 0.0f;
    }
    w_       = 0;
    hp_      = 0.0f;
    in_lp_   = 0.0f;
    hp_a_    = 1.0f - std::exp(-2.0f * kPi * 160.0f / sr);    /* keep the lows out    */
    in_lp_a_ = 1.0f - std::exp(-2.0f * kPi * 7000.0f / sr);   /* no fizz going in     */
    damp_a_  = 1.0f - std::exp(-2.0f * kPi * 4200.0f / sr);   /* highs die first      */
    lfo_       = 0.0f;
    lfo_inc_   = 0.37f / sr;                                  /* one drift per ~2.7 s */
    mod_depth_ = 9.0f * scale;                                /* +-9 samples          */
    wet_ = wet_target_ = 0.0f;
    Set(0.0f);
}

float Reverb::DecaySeconds(float amount)
{
    /* 0.3 s .. 3.5 s, evenly in ratio so the whole turn is useful. */
    return 0.3f * std::pow(3.5f / 0.3f, Clamp(amount, 0.0f, 1.0f));
}

void Reverb::Set(float amount)
{
    const float a = Clamp(amount, 0.0f, 1.0f);
    /* Level rises quickly at first, then the lengthening tail takes over. */
    wet_target_ = 1.8f * a * (1.6f - 0.6f * a);
    const float rt60 = DecaySeconds(a);
    for (int i = 0; i < kLines; i++)
        g_[i] = std::pow(10.0f, -3.0f * static_cast<float>(len_[i]) / (rt60 * sr_));
}

void Reverb::Tick(float in, float& out_l, float& out_r)
{
    constexpr int kMask = kMaxLine - 1;
    wet_ += 0.001f * (wet_target_ - wet_);

    hp_    += hp_a_ * (in - hp_);
    in_lp_ += in_lp_a_ * ((in - hp_) - in_lp_);
    float x = in_lp_;

    /* Diffusion: smear the hit before it reaches the room. */
    for (int i = 0; i < kNumAp; i++)
    {
        const float buf = ap_[i][ap_pos_[i]];
        const float y   = buf - 0.62f * x;
        ap_[i][ap_pos_[i]] = x + 0.62f * y;
        if (++ap_pos_[i] >= ap_len_[i]) ap_pos_[i] = 0;
        x = y;
    }

    lfo_ += lfo_inc_;
    if (lfo_ >= 1.0f) lfo_ -= 1.0f;
    const float tri = 4.0f * std::fabs(lfo_ - 0.5f) - 1.0f;       /* -1 .. +1 */

    float d[kLines];
    for (int i = 0; i < kLines; i++)
    {
        float v;
        if (i == 1 || i == 6)
        {
            /* Two lines drift in opposite directions (interpolated read). */
            const float delay = static_cast<float>(len_[i])
                              + (i == 1 ? tri : -tri) * mod_depth_;
            float rp = static_cast<float>(w_) - delay;
            if (rp < 0.0f) rp += static_cast<float>(kMaxLine);
            const int   i0 = static_cast<int>(rp);
            const float fr = rp - static_cast<float>(i0);
            const float a  = line_[i][i0 & kMask];
            const float b  = line_[i][(i0 + 1) & kMask];
            v = a + fr * (b - a);
        }
        else
        {
            v = line_[i][(w_ - len_[i]) & kMask];
        }
        lp_[i] += damp_a_ * (v - lp_[i]);
        if (lp_[i] < 1.0e-18f && lp_[i] > -1.0e-18f) lp_[i] = 0.0f;
        d[i] = lp_[i] * g_[i];
    }

    /* Hadamard mixing: every line feeds every other, equally. */
    float h[kLines];
    {
        const float a0 = d[0] + d[1], a1 = d[0] - d[1], a2 = d[2] + d[3], a3 = d[2] - d[3];
        const float a4 = d[4] + d[5], a5 = d[4] - d[5], a6 = d[6] + d[7], a7 = d[6] - d[7];
        const float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
        const float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
        h[0] = b0 + b4; h[1] = b1 + b5; h[2] = b2 + b6; h[3] = b3 + b7;
        h[4] = b0 - b4; h[5] = b1 - b5; h[6] = b2 - b6; h[7] = b3 - b7;
    }
    constexpr float kNorm = 0.35355339f;      /* 1 / sqrt(8) */
    const float xin = x * 0.35f;
    for (int i = 0; i < kLines; i++)
        line_[i][w_] = ((i & 1) ? -xin : xin) + h[i] * kNorm;
    w_ = (w_ + 1) & kMask;

    out_l = (d[0] - d[2] + d[4] - d[6]) * 0.5f * wet_;
    out_r = (d[1] - d[3] + d[5] - d[7]) * 0.5f * wet_;
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Rumble
 * ═════════════════════════════════════════════════════════════════════════ */

namespace {
inline void SetLp(Svf& f, float hz, float sr, float k = 1.41421356f)
{
    f.SetG(std::tan(kPi * Clamp(hz, 10.0f, 0.45f * sr) / sr));
    f.SetK(k);
}
} // namespace

void Rumble::Init(float sr, float* mem)
{
    sr_  = sr;
    srl_ = sr / static_cast<float>(kDecim);
    /* Mutually prime, 37 .. 79 ms: a dense, boomy room. */
    static const int kLineLen[kLines] = {443, 601, 757, 947};   /* at 12 kHz */
    static const int kApLen[kNumAp]   = {97, 163};
    const float scale = srl_ / 12000.0f;
    for (int i = 0; i < kLines; i++)
    {
        line_[i] = mem + i * kMaxLine;
        int n = static_cast<int>(static_cast<float>(kLineLen[i]) * scale);
        len_[i] = n < 8 ? 8 : (n > kMaxLine - 4 ? kMaxLine - 4 : n);
        damp_[i] = 0.0f;
        for (int k = 0; k < kMaxLine; k++) line_[i][k] = 0.0f;
    }
    for (int i = 0; i < kNumAp; i++)
    {
        ap_[i] = mem + kLines * kMaxLine + i * kMaxAp;
        int n = static_cast<int>(static_cast<float>(kApLen[i]) * scale);
        ap_len_[i] = n < 4 ? 4 : (n > kMaxAp ? kMaxAp : n);
        ap_pos_[i] = 0;
        for (int k = 0; k < kMaxAp; k++) ap_[i][k] = 0.0f;
    }
    w_ = 0;
    /* Anti-alias before dropping to 12 kHz, anti-image after coming back. */
    for (int i = 0; i < 2; i++) { pre_[i].Reset(); SetLp(pre_[i], 1500.0f, sr_); tone_[i].Reset(); }
    post_.Reset(); SetLp(post_, 900.0f, sr_);
    damp_a_ = 1.0f - std::exp(-2.0f * kPi * 900.0f / srl_);   /* dark tail */
    dc_a_   = std::exp(-2.0f * kPi * 20.0f / sr_);
    dc_x_ = dc_y_ = 0.0f;
    phase_ = 0;
    prev_ = cur_ = 0.0f;
    send_ = 0.0f;
    amount_ = amount_target_ = 0.0f;
    Set(RumbleParams{});
}

void Rumble::Set(const RumbleParams& p)
{
    send_target_   = p.on ? 1.0f : 0.0f;
    const float a  = Clamp(p.amount, 0.0f, 1.0f);
    amount_target_ = a * a * 1.1f;
    const float rt60 = Clamp(p.decay_s, 0.2f, 6.0f);
    for (int i = 0; i < kLines; i++)
        g_[i] = std::pow(10.0f, -3.0f * static_cast<float>(len_[i]) / (rt60 * srl_));
    /* Tone: a low-pass before the drive sets the rumble's colour, a second
     * one an octave and a bit above lets the drive's grit through but
     * nothing fizzy. */
    const float tone = Clamp(p.tone_hz, 30.0f, 600.0f);
    SetLp(tone_[0], tone, srl_);
    SetLp(tone_[1], tone * 2.5f, srl_);
    /* Drive: more gain into the shaper, the level pulled back so it reads
     * as more grit rather than much more volume. */
    const float d = Clamp(p.drive, 0.0f, 1.0f);
    drive_pre_  = 1.0f + d * d * 30.0f;     /* zero is clean */
    drive_post_ = std::pow(drive_pre_, -0.35f);
}

void Rumble::LowRate(float x)
{
    /* Diffuse, so the room swallows the hit rather than echoing it. */
    for (int i = 0; i < kNumAp; i++)
    {
        const float buf = ap_[i][ap_pos_[i]];
        const float y   = buf - 0.6f * x;
        ap_[i][ap_pos_[i]] = x + 0.6f * y;
        if (++ap_pos_[i] >= ap_len_[i]) ap_pos_[i] = 0;
        x = y;
    }

    constexpr int kMask = kMaxLine - 1;
    float d[kLines], sum = 0.0f;
    for (int i = 0; i < kLines; i++)
    {
        const float v = line_[i][(w_ - len_[i]) & kMask];
        damp_[i] += damp_a_ * (v - damp_[i]);
        if (damp_[i] < 1.0e-18f && damp_[i] > -1.0e-18f) damp_[i] = 0.0f;
        d[i] = damp_[i] * g_[i];
        sum += d[i];
    }
    sum *= 0.5f;                              /* Householder feedback */
    for (int i = 0; i < kLines; i++) line_[i][w_] = x + d[i] - sum;
    w_ = (w_ + 1) & kMask;

    float wet = (d[0] + d[1] + d[2] + d[3]) * 0.5f;

    /* Tone, drive, then a second low-pass to keep the grit dark. */
    float v1, v2;
    tone_[0].Tick(wet, v1, v2);
    wet = FastTanh(v2 * drive_pre_) * drive_post_;
    tone_[1].Tick(wet, v1, v2);

    prev_ = cur_;
    cur_  = v2;
}

float Rumble::Tick(float in, float pump)
{
    send_   += 0.0005f * (send_target_ - send_);         /* ~40 ms */
    amount_ += 0.0005f * (amount_target_ - amount_);

    float v1, v2;
    pre_[0].Tick(in * send_, v1, v2);
    pre_[1].Tick(v2, v1, v2);
    if (phase_ == 0) LowRate(v2);

    /* Back to full rate: linear interpolation, then a low-pass for images. */
    const float t = static_cast<float>(phase_ + 1) / static_cast<float>(kDecim);
    float y = prev_ + t * (cur_ - prev_);
    if (++phase_ >= kDecim) phase_ = 0;
    post_.Tick(y, v1, v2);
    y = v2;

    /* DC blocker: drive is asymmetric under some input. */
    const float hp = y - dc_x_ + dc_a_ * dc_y_;
    dc_x_ = y;
    dc_y_ = (hp < 1.0e-20f && hp > -1.0e-20f) ? 0.0f : hp;

    return dc_y_ * amount_ * pump;
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Duck envelope
 * ═════════════════════════════════════════════════════════════════════════ */

float DuckEnv::Advance(float dt, float attack_s, float hold_s, float release_s, float shape_k)
{
    switch (state_)
    {
        case kIdle:
            v_ = 0.0f;
            break;

        case kAttack:
            t_ += dt / (attack_s > 1.0e-4f ? attack_s : 1.0e-4f);
            if (t_ >= 1.0f)
            {
                v_     = 1.0f;
                t_     = 0.0f;
                state_ = (hold_s > 5.0e-4f) ? kHold : kRelease;
            }
            else
            {
                v_ = start_ + (1.0f - start_) * t_;
            }
            break;

        case kHold:
            v_  = 1.0f;
            t_ += dt / hold_s;
            if (t_ >= 1.0f) { t_ = 0.0f; state_ = kRelease; }
            break;

        case kRelease:
            t_ += dt / (release_s > 1.0e-3f ? release_s : 1.0e-3f);
            if (t_ >= 1.0f) { v_ = 0.0f; t_ = 0.0f; state_ = kIdle; }
            else            { v_ = std::pow(1.0f - t_, shape_k); }
            break;
    }
    return v_;
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Engine
 * ═════════════════════════════════════════════════════════════════════════ */

void Engine::Init(float sample_rate, float* mem)
{
    sr_ = sample_rate;
    kick_.Init(sr_);
    snare_.Init(sr_);
    reverb_.Init(sr_, mem);
    rumble_.Init(sr_, mem + Reverb::kMemFloats);
    pump_ = 1.0f;
    rumble_out_ = 0.0f;
    env_k_.Reset();
    env_s_.Reset();
    for (int c = 0; c < 2; c++)
    {
        split_[c].Reset();
        bell_k_[c].Reset();
        bell_s_[c].Reset();
    }
    gain_  = 1.0f;
    shape_ = 0.0f;
    w_[0] = 1.0f; w_[1] = 0.0f; w_[2] = 0.0f;
    split_hz_ = carve_k_hz_ = carve_s_hz_ = 0.0f;
    Set(Params{});
}

void Engine::Set(const Params& p)
{
    p_ = p;
    if (p_.duck.type < 0) p_.duck.type = 0;
    if (p_.duck.type >= kDuckTypeCount) p_.duck.type = kDuckTypeCount - 1;
    kick_.Set(p_.kick);
    snare_.Set(p_.snare);
    reverb_.Set(p_.reverb.amount);
    rumble_.Set(p_.rumble);
    UpdateFilters();
}

void Engine::UpdateFilters()
{
    const float focus  = Clamp(p_.duck.focus, 0.0f, 1.0f);
    const float nyq_ok = 0.45f * sr_;

    /* Width 0 = narrow (Q 3), 1 = wide (Q 0.35). Shared by Split and Carve. */
    carve_q_ = 3.0f * std::pow(0.35f / 3.0f, Clamp(p_.duck.width, 0.0f, 1.0f));

    /* Split: a band centred anywhere from 30 Hz to 2 kHz (245 Hz at noon),
     * independent of the drums' tuning. */
    const float split = Clamp(30.0f * std::pow(2000.0f / 30.0f, focus), 20.0f, nyq_ok);
    if (split != split_hz_)
    {
        split_hz_ = split;
        split_g_  = std::tan(kPi * split / sr_);
    }
    for (int c = 0; c < 2; c++)
    {
        split_[c].SetG(split_g_);
        split_[c].SetK(1.0f / carve_q_);
    }

    /* Carve: each cut sits just above its drum's resting pitch (where the
     * sweep spends its energy); Focus slides both by up to an octave. */
    const float shift = std::pow(2.0f, (focus - 0.5f) * 2.0f);
    const float fk    = Clamp(p_.kick.tune_hz * 1.4f * shift, 20.0f, nyq_ok);
    const float fs    = Clamp(p_.snare.tune_hz * 1.25f * shift, 20.0f, nyq_ok);
    if (fk != carve_k_hz_) { carve_k_hz_ = fk; carve_k_g_ = std::tan(kPi * fk / sr_); }
    if (fs != carve_s_hz_) { carve_s_hz_ = fs; carve_s_g_ = std::tan(kPi * fs / sr_); }
}

void Engine::TriggerKick(float vel)
{
    kick_.Trigger(vel);
    env_k_.Trigger();
}

void Engine::TriggerSnare(float vel)
{
    snare_.Trigger(vel);
    env_s_.Trigger();
}

void Engine::Process(const float* in_l, const float* in_r,
                     float* out_l, float* out_r, size_t n)
{
    constexpr size_t kChunk = 16;          /* envelope / coefficient rate */
    constexpr float  kMinA  = 0.126f;      /* deepest carve: -36 dB       */

    const DuckParams& d = p_.duck;
    /* Curve 0..1 -> release exponent 0.25 (late) .. 1 (linear) .. 4 (early). */
    const float shape_k = std::pow(2.0f, (Clamp(d.curve, 0.0f, 1.0f) - 0.5f) * 4.0f);
    const float depth   = Clamp(d.depth, 0.0f, 1.0f);
    const float amt_k   = Clamp(d.kick_amt, 0.0f, 1.0f);
    const float amt_s   = Clamp(d.snare_amt, 0.0f, 1.0f);
    const float rev_kick = p_.reverb.kick ? 0.6f : 0.0f;

    size_t done = 0;
    while (done < n)
    {
        const size_t m    = (n - done < kChunk) ? (n - done) : kChunk;
        const float  dt   = static_cast<float>(m) / sr_;
        const float  inv  = 1.0f / static_cast<float>(m);

        const float ek = env_k_.Advance(dt, d.attack_s, d.hold_s, d.release_s, shape_k);
        const float es = env_s_.Advance(dt, d.attack_s, d.hold_s, d.release_s, shape_k);

        /* Amplitude factors per drum; the audible gain is their square,
         * so Depth reads roughly in dB and reaches full mute at the top. */
        const float ak = 1.0f - depth * amt_k * ek;
        const float as = 1.0f - depth * amt_s * es;
        const float g_target = (ak * as) * (ak * as);
        const float g_step   = (g_target - gain_) * inv;

        /* Rumble pump: the kick's envelope at full depth, whatever Depth
         * is set to, so the rumble always breathes with the kick. */
        const float pump_target = (1.0f - ek) * (1.0f - ek);
        const float pump_step   = (pump_target - pump_) * inv;

        const float sk = ek * amt_k, ss = es * amt_s;
        shape_ = sk > ss ? sk : ss;

        /* Bells: gain at the centre = A^2, so A is the amplitude factor. */
        {
            const float a_k = ak > kMinA ? ak : kMinA;
            const float a_s = as > kMinA ? as : kMinA;
            const float k_k = 1.0f / (carve_q_ * a_k);
            const float k_s = 1.0f / (carve_q_ * a_s);
            for (int c = 0; c < 2; c++)
            {
                bell_k_[c].SetG(carve_k_g_); bell_k_[c].SetK(k_k);
                bell_s_[c].SetG(carve_s_g_); bell_s_[c].SetK(k_s);
            }
            bell_k_m1_ = k_k * (a_k * a_k - 1.0f);
            bell_s_m1_ = k_s * (a_s * a_s - 1.0f);
        }

        /* Crossfade between duck types over ~12 ms. */
        float w_step[kDuckTypeCount];
        {
            const float a = 1.0f - std::exp(-dt / 0.012f);
            for (int t = 0; t < kDuckTypeCount; t++)
            {
                const float target = (t == d.type) ? 1.0f : 0.0f;
                float next = w_[t] + a * (target - w_[t]);
                if (std::fabs(target - next) < 1.0e-4f) next = target;
                w_step[t] = (next - w_[t]) * inv;
            }
        }

        float g  = gain_;
        float pump = pump_;
        float w0 = w_[0], w1 = w_[1], w2 = w_[2];
        float rmax = 0.0f;

        for (size_t i = 0; i < m; i++)
        {
            g    += g_step;
            pump += pump_step;
            w0 += w_step[0];
            w1 += w_step[1];
            w2 += w_step[2];

            const float kick  = kick_.Tick();
            const float snare = snare_.Tick();
            const float rumble = rumble_.Tick(kick, pump);
            const float drums  = kick + snare + rumble;
            const float ra     = std::fabs(rumble);
            if (ra > rmax) rmax = ra;
            float rev_l, rev_r;
            reverb_.Tick(snare + kick * rev_kick, rev_l, rev_r);
            const float x[2]  = {in_l[done + i], in_r[done + i]};
            float       y[2];

            for (int c = 0; c < 2; c++)
            {
                float v1, v2;

                /* Split: take the band out by the ducking amount. k * v1 is
                 * the band-pass at unity gain, so the centre goes to g, the
                 * same dip Volume gives, and at rest (g = 1) it is exact. */
                split_[c].Tick(x[c], v1, v2);
                const float split = x[c] - (1.0f - g) * split_[c].k * v1;

                /* Carve */
                bell_k_[c].Tick(x[c], v1, v2);
                float carve = x[c] + bell_k_m1_ * v1;
                bell_s_[c].Tick(carve, v1, v2);
                carve += bell_s_m1_ * v1;

                y[c] = w0 * (x[c] * g) + w1 * split + w2 * carve;
            }

            out_l[done + i] = SoftClip(y[0] + drums + rev_l);
            out_r[done + i] = SoftClip(y[1] + drums + rev_r);
        }

        gain_ = g_target;
        pump_ = pump_target;
        rumble_out_ = rmax > rumble_out_ ? rmax : rumble_out_ * 0.97f;
        for (int t = 0; t < kDuckTypeCount; t++)
            w_[t] += w_step[t] * static_cast<float>(m);

        done += m;
    }
}

} // namespace blas
