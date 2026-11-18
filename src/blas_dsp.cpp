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
    punch_coef_   = CoefTau(0.004f, sr);     /* the knock lasts a few ms */
    click_coef_   = CoefTau(0.0015f, sr);
    declick_coef_ = CoefTau(0.0015f, sr);
    Set(KickParams{});
}

void Kick::Set(const KickParams& p)
{
    const float tune  = Clamp(p.tune_hz, 20.0f, 400.0f);
    const float decay = Clamp(p.decay_s, 0.03f, 5.0f);
    const float bend  = Clamp(p.bend, 0.0f, 1.0f);
    const float punch = Clamp(p.punch, 0.0f, 1.0f);
    const float drive = Clamp(p.drive, 0.0f, 1.0f);
    const float level = Clamp(p.level, 0.0f, 1.0f);

    inc_      = tune / sr_;
    amp_coef_ = Coef60(decay, sr_);

    /* Bend: the classic slow sweep. More bend is deeper and a little longer. */
    bend_amt_  = bend * bend * 6.0f;
    bend_coef_ = CoefTau(0.020f + 0.050f * bend, sr_);

    /* Punch: a very fast pitch spike plus a level bump into the saturator. */
    punch_pitch_ = punch * 3.0f;
    punch_amp_   = punch;

    click_gain_ = Clamp(p.click, 0.0f, 1.0f) * 0.7f;

    drive_g_    = 1.0f + drive * drive * 14.0f;
    drive_norm_ = 1.0f / FastTanh(drive_g_);

    level_       = level * level;
    gain_target_ = level_ * vel_;
}

void Kick::Trigger(float vel)
{
    vel_         = Clamp(vel, 0.0f, 1.0f);
    gain_target_ = level_ * vel_;
    gain_        = gain_target_;
    /* Restart the body from zero phase so every hit lands the same, and
     * carry the old tail's last value out as a short decaying offset so
     * the restart cannot click. */
    declick_   = last_;
    phase_     = 0.0f;
    amp_       = 1.0f;
    env_bend_  = 1.0f;
    env_punch_ = 1.0f;
    env_click_ = 1.0f;
    click_lp_  = 0.0f;
}

float Kick::Tick()
{
    if (amp_ < kSilence && env_click_ < kSilence
        && declick_ < kSilence && declick_ > -kSilence)
    {
        amp_ = env_click_ = env_bend_ = env_punch_ = 0.0f;
        declick_ = 0.0f;
        last_    = 0.0f;
        return 0.0f;
    }

    gain_ += 0.002f * (gain_target_ - gain_);

    const float pitch = 1.0f + bend_amt_ * env_bend_ + punch_pitch_ * env_punch_;
    phase_ += inc_ * pitch;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    if (phase_ >= 1.0f) phase_ = 0.0f;

    float body = Sin01(phase_) * amp_ * (1.0f + punch_amp_ * env_punch_);
    body = FastTanh(body * drive_g_) * drive_norm_;

    const float click = (noise_.Next() * 0.6f + 0.8f) * env_click_;
    click_lp_ += 0.5f * (click - click_lp_);

    amp_       *= amp_coef_;
    env_bend_  *= bend_coef_;
    env_punch_ *= punch_coef_;
    env_click_ *= click_coef_;

    float out = (body + click_lp_ * click_gain_) * gain_;
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
    declick_coef_ = CoefTau(0.0015f, sr);
    Set(SnareParams{});
}

void Snare::Set(const SnareParams& p)
{
    const float tune   = Clamp(p.tune_hz, 60.0f, 800.0f);
    const float decay  = Clamp(p.decay_s, 0.03f, 3.0f);
    const float snappy = Clamp(p.snappy, 0.0f, 1.0f);
    const float tone   = Clamp(p.tone, 0.0f, 1.0f);
    const float drive  = Clamp(p.drive, 0.0f, 1.0f);
    const float level  = Clamp(p.level, 0.0f, 1.0f);

    inc1_ = tune / sr_;
    inc2_ = inc1_ * 1.59f;                 /* the two detuned shell modes */

    c1_      = Coef60(decay, sr_);
    c2_      = Coef60(decay * 0.6f, sr_);
    c_noise_ = Coef60(decay * 1.4f, sr_);

    pitch_amt_ = Clamp(p.bend, 0.0f, 1.0f) * 1.2f;

    /* Tone: the noise low-pass opens from 2.5 kHz to 11 kHz while the
     * high-pass rises, so bright settings are thinner as well as brighter. */
    const float lp_hz = 2500.0f * std::pow(4.4f, tone);
    const float hp_hz = 250.0f + 1200.0f * tone;
    lp_a_ = 1.0f - std::exp(-2.0f * kPi * lp_hz / sr_);
    hp_a_ = 1.0f - std::exp(-2.0f * kPi * hp_hz / sr_);

    noise_gain_ = snappy * 2.2f;
    body_gain_  = 1.0f - 0.35f * snappy;

    drive_g_    = 1.0f + drive * drive * 10.0f;
    drive_norm_ = 1.0f / FastTanh(drive_g_);

    level_       = level * level;
    gain_target_ = level_ * vel_;
}

void Snare::Trigger(float vel)
{
    vel_         = Clamp(vel, 0.0f, 1.0f);
    gain_target_ = level_ * vel_;
    gain_        = gain_target_;
    declick_     = last_;
    p1_ = p2_ = 0.0f;
    e1_ = e2_ = e_noise_ = e_pitch_ = 1.0f;
}

float Snare::Tick()
{
    if (e1_ < kSilence && e_noise_ < kSilence
        && declick_ < kSilence && declick_ > -kSilence)
    {
        e1_ = e2_ = e_noise_ = e_pitch_ = 0.0f;
        declick_ = 0.0f;
        last_    = 0.0f;
        return 0.0f;
    }

    gain_ += 0.002f * (gain_target_ - gain_);

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

    float x = body * 0.8f + snares;
    x = FastTanh(x * drive_g_) * drive_norm_;

    e1_      *= c1_;
    e2_      *= c2_;
    e_noise_ *= c_noise_;
    e_pitch_ *= c_pitch_;

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

void Engine::Init(float sample_rate, float* reverb_mem)
{
    sr_ = sample_rate;
    kick_.Init(sr_);
    snare_.Init(sr_);
    reverb_.Init(sr_, reverb_mem);
    env_k_.Reset();
    env_s_.Reset();
    for (int c = 0; c < 2; c++)
    {
        for (int s = 0; s < 2; s++) { lp_[c][s].Reset(); hp_[c][s].Reset(); }
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
    UpdateFilters();
}

void Engine::UpdateFilters()
{
    const float focus  = Clamp(p_.duck.focus, 0.0f, 1.0f);
    const float nyq_ok = 0.45f * sr_;

    /* Split: crossover 50 Hz .. 1 kHz, 224 Hz at noon. */
    const float split = Clamp(50.0f * std::pow(20.0f, focus), 20.0f, nyq_ok);
    if (split != split_hz_)
    {
        split_hz_ = split;
        split_g_  = std::tan(kPi * split / sr_);
        for (int c = 0; c < 2; c++)
            for (int s = 0; s < 2; s++)
            {
                lp_[c][s].SetG(split_g_); lp_[c][s].SetK(1.41421356f);
                hp_[c][s].SetG(split_g_); hp_[c][s].SetK(1.41421356f);
            }
    }

    /* Carve: each cut sits just above its drum's resting pitch (where the
     * sweep spends its energy); Focus slides both by up to an octave. */
    const float shift = std::pow(2.0f, (focus - 0.5f) * 2.0f);
    const float fk    = Clamp(p_.kick.tune_hz * 1.4f * shift, 20.0f, nyq_ok);
    const float fs    = Clamp(p_.snare.tune_hz * 1.25f * shift, 20.0f, nyq_ok);
    if (fk != carve_k_hz_) { carve_k_hz_ = fk; carve_k_g_ = std::tan(kPi * fk / sr_); }
    if (fs != carve_s_hz_) { carve_s_hz_ = fs; carve_s_g_ = std::tan(kPi * fs / sr_); }

    /* Width 0 = narrow (Q 3), 1 = wide (Q 0.35). */
    carve_q_ = 3.0f * std::pow(0.35f / 3.0f, Clamp(p_.duck.width, 0.0f, 1.0f));
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
        float w0 = w_[0], w1 = w_[1], w2 = w_[2];

        for (size_t i = 0; i < m; i++)
        {
            g  += g_step;
            w0 += w_step[0];
            w1 += w_step[1];
            w2 += w_step[2];

            const float kick  = kick_.Tick();
            const float snare = snare_.Tick();
            const float drums = kick + snare;
            float rev_l, rev_r;
            reverb_.Tick(snare + kick * rev_kick, rev_l, rev_r);
            const float x[2]  = {in_l[done + i], in_r[done + i]};
            float       y[2];

            for (int c = 0; c < 2; c++)
            {
                float v1, v2;

                /* Split */
                lp_[c][0].Tick(x[c], v1, v2);
                lp_[c][1].Tick(v2, v1, v2);
                const float low = v2;
                hp_[c][0].Tick(x[c], v1, v2);
                float high = x[c] - hp_[c][0].k * v1 - v2;
                hp_[c][1].Tick(high, v1, v2);
                high = high - hp_[c][1].k * v1 - v2;

                /* Carve */
                bell_k_[c].Tick(x[c], v1, v2);
                float carve = x[c] + bell_k_m1_ * v1;
                bell_s_[c].Tick(carve, v1, v2);
                carve += bell_s_m1_ * v1;

                y[c] = w0 * (x[c] * g) + w1 * (low * g + high) + w2 * carve;
            }

            out_l[done + i] = SoftClip(y[0] + drums + rev_l);
            out_r[done + i] = SoftClip(y[1] + drums + rev_r);
        }

        gain_ = g_target;
        for (int t = 0; t < kDuckTypeCount; t++)
            w_[t] += w_step[t] * static_cast<float>(m);

        done += m;
    }
}

} // namespace blas
