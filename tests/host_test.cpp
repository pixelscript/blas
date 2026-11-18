/**
 * Host-side checks for the Blas audio engine (no hardware needed).
 *
 *   make test
 *
 * Each check prints what it measured; any failure exits non-zero.
 */
#include <cmath>
#include <cstdio>
#include <vector>

#include "blas_dsp.h"

using namespace blas;

static int   g_fail = 0;
static const float kSr = 48000.0f;
static const float kPi = 3.14159265358979f;

static void Check(bool ok, const char* what, double got, const char* unit = "")
{
    std::printf("  %s %-58s %10.4f %s\n", ok ? "ok  " : "FAIL", what, got, unit);
    if (!ok) g_fail++;
}

static double Db(double x) { return 20.0 * std::log10(x > 1e-12 ? x : 1e-12); }

struct Run
{
    std::vector<float> l, r;
    double Peak(size_t a, size_t b) const
    {
        double p = 0;
        for (size_t i = a; i < b && i < l.size(); i++) p = std::fmax(p, std::fabs(l[i]));
        return p;
    }
    double Rms(size_t a, size_t b) const
    {
        double s = 0; size_t n = 0;
        for (size_t i = a; i < b && i < l.size(); i++) { s += (double)l[i] * l[i]; n++; }
        return n ? std::sqrt(s / n) : 0;
    }
    bool Finite() const
    {
        for (float v : l) if (!std::isfinite(v)) return false;
        for (float v : r) if (!std::isfinite(v)) return false;
        return true;
    }
};

/** Render @p seconds of a sine at @p hz (amp @p amp; 0 Hz = silence),
 *  firing kick/snare at the given sample offsets (-1 = never). */
static Run Render(Engine& e, const Params& p, float hz, float amp, float seconds,
                  long kick_at, long snare_at, size_t block = 24)
{
    Run out;
    const size_t n = (size_t)(seconds * kSr);
    out.l.resize(n); out.r.resize(n);
    std::vector<float> il(block), ir(block);
    size_t pos = 0;
    while (pos < n)
    {
        const size_t m = (n - pos < block) ? n - pos : block;
        for (size_t i = 0; i < m; i++)
            il[i] = ir[i] = (hz > 0) ? amp * std::sin(2.0f * kPi * hz * (float)(pos + i) / kSr) : 0.0f;
        e.Set(p);
        if (kick_at >= 0 && (size_t)kick_at >= pos && (size_t)kick_at < pos + m) e.TriggerKick(1.0f);
        if (snare_at >= 0 && (size_t)snare_at >= pos && (size_t)snare_at < pos + m) e.TriggerSnare(1.0f);
        e.Process(il.data(), ir.data(), &out.l[pos], &out.r[pos], m);
        pos += m;
    }
    return out;
}

/** Dominant frequency over [a, b) from positive-going zero crossings. */
static double ZeroCrossHz(const Run& r, size_t a, size_t b)
{
    int count = 0; long first = -1, last = -1;
    for (size_t i = a + 1; i < b && i < r.l.size(); i++)
        if (r.l[i - 1] < 0 && r.l[i] >= 0)
        {
            if (first < 0) first = (long)i;
            last = (long)i; count++;
        }
    if (count < 2) return 0;
    return (double)(count - 1) * kSr / (double)(last - first);
}

static Params Quiet()
{
    Params p;
    p.kick.level  = 0.0f;
    p.snare.level = 0.0f;
    return p;
}

int main()
{
    /* ── 1. Drums ─────────────────────────────────────────────────────── */
    std::printf("Kick\n");
    {
        Engine e; e.Init(kSr);
        Params p; p.duck.depth = 0; p.snare.level = 0;
        p.kick.level = 1.0f; p.kick.tune_hz = 50.0f; p.kick.decay_s = 0.5f;
        Run r = Render(e, p, 0, 0, 2.0f, 480, -1);
        Check(r.Finite(), "output is finite", 1);
        Check(r.Peak(0, 479) == 0.0, "silent before the trigger", r.Peak(0, 479));
        const double pk = r.Peak(480, 48000);
        Check(pk > 0.6 && pk <= 1.0, "peak at full level", pk);
        const double tail = ZeroCrossHz(r, 480 + 9600, 480 + 16800);
        Check(std::fabs(tail - 50.0) < 3.0, "tail settles on the tuned pitch", tail, "Hz");
        const double early = ZeroCrossHz(r, 480, 480 + 1440);
        Check(early > 80.0, "pitch starts well above the tuning", early, "Hz");
        Check(Db(r.Rms(480 + 24000, 480 + 26400)) < -55.0, "decayed after its decay time",
              Db(r.Rms(480 + 24000, 480 + 26400)), "dB");
        Check(r.Peak(72000, 96000) == 0.0, "returns to exact silence", r.Peak(72000, 96000));

        /* Level and drive stay bounded. */
        p.kick.drive = 1.0f; p.kick.punch = 1.0f; p.kick.click = 1.0f; p.kick.bend = 1.0f;
        Engine e2; e2.Init(kSr);
        Run r2 = Render(e2, p, 0, 0, 1.0f, 100, -1);
        Check(r2.Finite() && r2.Peak(0, 48000) <= 1.0, "everything at maximum stays within +-1",
              r2.Peak(0, 48000));

        /* Rapid retriggers do not blow up or click to full scale. */
        Engine e3; e3.Init(kSr);
        Run acc;
        for (int k = 0; k < 16; k++)
        {
            Run part = Render(e3, p, 0, 0, 0.03f, 10, -1);
            acc.l.insert(acc.l.end(), part.l.begin(), part.l.end());
            acc.r.insert(acc.r.end(), part.r.begin(), part.r.end());
        }
        Check(acc.Finite() && acc.Peak(0, acc.l.size()) <= 1.0, "30 ms retriggers stay bounded",
              acc.Peak(0, acc.l.size()));
    }

    std::printf("Snare\n");
    {
        Engine e; e.Init(kSr);
        Params p; p.duck.depth = 0; p.kick.level = 0; p.snare.level = 1.0f;
        Run r = Render(e, p, 0, 0, 1.5f, -1, 480);
        const double pk = r.Peak(480, 48000);
        Check(r.Finite(), "output is finite", 1);
        Check(pk > 0.4 && pk <= 1.0, "peak at full level", pk);
        Check(r.Peak(60000, 72000) == 0.0, "returns to exact silence", r.Peak(60000, 72000));

        p.snare.snappy = 0.0f;
        Engine e2; e2.Init(kSr);
        Run body = Render(e2, p, 0, 0, 0.5f, -1, 100);
        p.snare.snappy = 1.0f;
        Engine e3; e3.Init(kSr);
        Run snap = Render(e3, p, 0, 0, 0.5f, -1, 100);
        /* Snare noise lives in the tail: compare 60-120 ms after the hit. */
        Check(snap.Rms(100 + 2880, 100 + 5760) > 1.5 * body.Rms(100 + 2880, 100 + 5760),
              "Snappy adds noise to the tail (ratio)",
              snap.Rms(100 + 2880, 100 + 5760) / std::fmax(body.Rms(100 + 2880, 100 + 5760), 1e-9));
    }

    /* ── 2. Ducker: Volume ────────────────────────────────────────────── */
    std::printf("Duck: Volume\n");
    {
        Engine e; e.Init(kSr);
        Params p = Quiet();
        p.duck.type = kDuckVolume; p.duck.depth = 0.5f; p.duck.kick_amt = 1.0f;
        p.duck.attack_s = 0.002f; p.duck.hold_s = 0.05f; p.duck.release_s = 0.2f;
        Run r = Render(e, p, 1000, 0.5f, 1.0f, 4800, -1);
        Check(std::fabs(Db(r.Rms(0, 4800) / (0.5 / std::sqrt(2.0)))) < 0.05,
              "unity gain at rest", Db(r.Rms(0, 4800) / (0.5 / std::sqrt(2.0))), "dB");
        /* Held at the bottom: (1 - 0.5)^2 = 0.25 = -12.04 dB. */
        const double held = Db(r.Rms(4800 + 480, 4800 + 1920) / (0.5 / std::sqrt(2.0)));
        Check(std::fabs(held + 12.04) < 0.2, "depth 50% ducks by 12 dB while held", held, "dB");
        const double back = Db(r.Rms(4800 + 14400, 4800 + 19200) / (0.5 / std::sqrt(2.0)));
        Check(std::fabs(back) < 0.05, "fully recovered after attack+hold+release", back, "dB");

        /* Depth 100% reaches silence; depth 0 does nothing. */
        Engine e2; e2.Init(kSr);
        p.duck.depth = 1.0f;
        Run r2 = Render(e2, p, 1000, 0.5f, 0.2f, 480, -1);
        Check(Db(r2.Rms(480 + 480, 480 + 1920)) < -80.0, "depth 100% mutes", Db(r2.Rms(480 + 480, 480 + 1920)), "dB");
        Engine e3; e3.Init(kSr);
        p.duck.depth = 0.0f;
        Run r3 = Render(e3, p, 1000, 0.5f, 0.2f, 480, -1);
        Check(std::fabs(Db(r3.Rms(960, 2400) / (0.5 / std::sqrt(2.0)))) < 0.05, "depth 0 leaves the signal alone",
              Db(r3.Rms(960, 2400) / (0.5 / std::sqrt(2.0))), "dB");

        /* Snare amount scales the snare's ducking independently. */
        Engine e4; e4.Init(kSr);
        p.duck.depth = 0.5f; p.duck.snare_amt = 0.5f;
        Run r4 = Render(e4, p, 1000, 0.5f, 0.2f, -1, 480);
        const double sn = Db(r4.Rms(480 + 480, 480 + 1920) / (0.5 / std::sqrt(2.0)));
        Check(std::fabs(sn - Db(0.75 * 0.75)) < 0.2, "snare at 50% amount ducks by (1-0.25)^2", sn, "dB");

        /* All of the above ran with the kick's level at zero: a trigger
         * ducks whether or not its drum is audible (a "ghost" kick). */
        Check(r.Peak(0, r.l.size()) <= 0.5001, "ghost trigger: silent kick, ducking only", r.Peak(0, r.l.size()));
    }

    /* ── 3. Ducker: Split ─────────────────────────────────────────────── */
    std::printf("Duck: Split\n");
    {
        Params p = Quiet();
        p.duck.type = kDuckSplit; p.duck.depth = 0.8f; p.duck.focus = 0.5f;
        p.duck.attack_s = 0.002f; p.duck.hold_s = 0.2f; p.duck.release_s = 0.2f;
        const double ref = 0.5 / std::sqrt(2.0);

        /* At rest the two bands must sum flat at every frequency. */
        const float freqs[] = {40, 100, 224, 500, 2000, 8000};
        double worst = 0;
        for (float f : freqs)
        {
            Engine e; e.Init(kSr);
            Run r = Render(e, p, f, 0.5f, 1.0f, -1, -1);
            worst = std::fmax(worst, std::fabs(Db(r.Rms(24000, 48000) / ref)));
        }
        Check(worst < 0.1, "bands sum flat at rest (worst deviation)", worst, "dB");

        Engine lo; lo.Init(kSr);
        Run rl = Render(lo, p, 50, 0.5f, 1.0f, 24000, -1);
        const double low_cut = Db(rl.Rms(24000 + 2400, 24000 + 7200) / ref);
        Check(low_cut < -20.0, "50 Hz is ducked hard", low_cut, "dB");

        Engine hi; hi.Init(kSr);
        Run rh = Render(hi, p, 4000, 0.5f, 1.0f, 24000, -1);
        const double hi_cut = Db(rh.Rms(24000 + 2400, 24000 + 7200) / ref);
        Check(std::fabs(hi_cut) < 0.3, "4 kHz passes untouched", hi_cut, "dB");
        Check(std::fabs(lo.SplitHz() - 223.6) < 2.0, "Focus at noon puts the crossover near 224 Hz", lo.SplitHz(), "Hz");
    }

    /* ── 4. Ducker: Carve ─────────────────────────────────────────────── */
    std::printf("Duck: Carve\n");
    {
        Params p = Quiet();
        p.duck.type = kDuckCarve; p.duck.depth = 0.5f; p.duck.focus = 0.5f; p.duck.width = 0.6f;
        p.duck.attack_s = 0.002f; p.duck.hold_s = 0.3f; p.duck.release_s = 0.2f;
        p.kick.tune_hz = 50.0f; p.snare.tune_hz = 200.0f; p.duck.snare_amt = 1.0f;
        const double ref = 0.5 / std::sqrt(2.0);

        Engine probe; probe.Init(kSr); probe.Set(p);
        const float fk = probe.KickCarveHz(), fs = probe.SnareCarveHz();
        Check(std::fabs(fk - 70.0f) < 0.5f, "kick cut sits at 1.4x the kick tuning", fk, "Hz");
        Check(std::fabs(fs - 250.0f) < 0.5f, "snare cut sits at 1.25x the snare tuning", fs, "Hz");

        Engine a; a.Init(kSr);
        Run ra = Render(a, p, fk, 0.5f, 1.0f, 24000, -1);
        const double at_k = Db(ra.Rms(24000 + 4800, 24000 + 12000) / ref);
        Check(std::fabs(at_k + 12.04) < 0.5, "kick trigger cuts 12 dB at the kick centre", at_k, "dB");
        Check(std::fabs(Db(ra.Rms(4800, 24000) / ref)) < 0.05, "flat at rest", Db(ra.Rms(4800, 24000) / ref), "dB");

        Engine b; b.Init(kSr);
        Run rb = Render(b, p, 5000, 0.5f, 1.0f, 24000, -1);
        const double far = Db(rb.Rms(24000 + 4800, 24000 + 12000) / ref);
        Check(std::fabs(far) < 0.3, "5 kHz is left alone", far, "dB");

        Engine c; c.Init(kSr);
        Run rc = Render(c, p, fs, 0.5f, 1.0f, -1, 24000);
        const double at_s = Db(rc.Rms(24000 + 4800, 24000 + 12000) / ref);
        Check(std::fabs(at_s + 12.04) < 0.5, "snare trigger cuts 12 dB at the snare centre", at_s, "dB");

        Engine d2; d2.Init(kSr);
        Run rd = Render(d2, p, fs, 0.5f, 1.0f, 24000, -1);
        const double kick_on_snare_band = Db(rd.Rms(24000 + 4800, 24000 + 12000) / ref);
        Check(kick_on_snare_band > -4.0, "kick trigger barely touches the snare band", kick_on_snare_band, "dB");
    }

    /* ── 5. Type changes and robustness ───────────────────────────────── */
    std::printf("Robustness\n");
    {
        /* Switching type on a steady tone must not click: the largest
         * sample-to-sample jump stays close to the tone's own slope. */
        Engine e; e.Init(kSr);
        Params p = Quiet(); p.duck.depth = 0.5f;
        std::vector<float> all;
        for (int k = 0; k < 9; k++)
        {
            p.duck.type = k % 3;
            Run r = Render(e, p, 200, 0.5f, 0.1f, -1, -1);
            all.insert(all.end(), r.l.begin(), r.l.end());
        }
        double max_step = 0;
        for (size_t i = 4800; i < all.size(); i++)
            max_step = std::fmax(max_step, std::fabs(all[i] - all[i - 1]));
        const double tone_step = 0.5 * 2.0 * kPi * 200.0 / kSr;
        Check(max_step < 2.0 * tone_step, "no click when the duck type changes (step / tone slope)",
              max_step / tone_step);

        /* Wild parameters and odd block sizes stay finite and bounded. */
        Engine w; w.Init(kSr);
        Params q;
        q.kick.tune_hz = 5000; q.kick.decay_s = 0; q.kick.drive = 9; q.kick.level = 3;
        q.snare.tune_hz = -4; q.snare.decay_s = 100; q.snare.tone = 7;
        q.duck.attack_s = 0; q.duck.hold_s = -1; q.duck.release_s = 0; q.duck.depth = 4;
        q.duck.type = 17; q.duck.focus = -3; q.duck.width = 9; q.duck.curve = -2;
        bool ok = true; double peak = 0;
        const size_t blocks[] = {1, 7, 24, 64, 333};
        for (size_t b : blocks)
        {
            Run r = Render(w, q, 100, 2.0f, 0.3f, 5, 9, b);
            ok = ok && r.Finite();
            peak = std::fmax(peak, r.Peak(0, r.l.size()));
        }
        Check(ok && peak <= 1.0, "out-of-range parameters stay finite and within +-1", peak);
    }

    std::printf(g_fail ? "\n%d check(s) FAILED\n" : "\nAll checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
