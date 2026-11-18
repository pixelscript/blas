/**
 * Host-side checks for the Blas audio engine (no hardware needed).
 *
 *   make test
 *
 * Each check prints what it measured; any failure exits non-zero.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "blas_dsp.h"

using namespace blas;

static int   g_fail = 0;
static const float kSr = 48000.0f;
static const float kPi = 3.14159265358979f;

/** Fresh, deliberately dirty delay memory for one engine. */
static float* NewReverbMem()
{
    static std::vector<std::vector<float>> pool;
    pool.emplace_back(Reverb::kMemFloats, 123.0f);
    return pool.back().data();
}

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
        Engine e; e.Init(kSr, NewReverbMem());
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
        Engine e2; e2.Init(kSr, NewReverbMem());
        Run r2 = Render(e2, p, 0, 0, 1.0f, 100, -1);
        Check(r2.Finite() && r2.Peak(0, 48000) <= 1.0, "everything at maximum stays within +-1",
              r2.Peak(0, 48000));

        /* Rapid retriggers do not blow up or click to full scale. */
        Engine e3; e3.Init(kSr, NewReverbMem());
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
        Engine e; e.Init(kSr, NewReverbMem());
        Params p; p.duck.depth = 0; p.kick.level = 0; p.snare.level = 1.0f;
        Run r = Render(e, p, 0, 0, 1.5f, -1, 480);
        const double pk = r.Peak(480, 48000);
        Check(r.Finite(), "output is finite", 1);
        Check(pk > 0.4 && pk <= 1.0, "peak at full level", pk);
        Check(r.Peak(60000, 72000) == 0.0, "returns to exact silence", r.Peak(60000, 72000));

        p.snare.snappy = 0.0f;
        Engine e2; e2.Init(kSr, NewReverbMem());
        Run body = Render(e2, p, 0, 0, 0.5f, -1, 100);
        p.snare.snappy = 1.0f;
        Engine e3; e3.Init(kSr, NewReverbMem());
        Run snap = Render(e3, p, 0, 0, 0.5f, -1, 100);
        /* Snare noise lives in the tail: compare 60-120 ms after the hit. */
        Check(snap.Rms(100 + 2880, 100 + 5760) > 1.5 * body.Rms(100 + 2880, 100 + 5760),
              "Snappy adds noise to the tail (ratio)",
              snap.Rms(100 + 2880, 100 + 5760) / std::fmax(body.Rms(100 + 2880, 100 + 5760), 1e-9));
    }

    std::printf("Reverb\n");
    {
        Params p; p.duck.depth = 0; p.kick.level = 1.0f; p.snare.level = 1.0f;
        const size_t hit = 480;
        auto ms = [&](float t) { return hit + (size_t)(t * 48.0f); };

        Engine dry; dry.Init(kSr, NewReverbMem());
        Run rd = Render(dry, p, 0, 0, 8.0f, -1, (long)hit);

        /* Three settings: level and decay must both rise with the knob. */
        const float amounts[3] = {0.2f, 0.5f, 1.0f};
        Run   r[3];
        double early[3], rt[3];
        for (int k = 0; k < 3; k++)
        {
            p.reverb.amount = amounts[k];
            Engine e; e.Init(kSr, NewReverbMem());
            r[k] = Render(e, p, 0, 0, 8.0f, -1, (long)hit);
            /* The dry snare is gone by 400 ms, so from there it is all reverb. */
            early[k] = Db(r[k].Rms(ms(400), ms(450)));
            /* Measured decay: time for the tail to fall 30 dB from there, doubled. */
            rt[k] = 0;
            for (float t = 450; t < 7500; t += 25)
                if (Db(r[k].Rms(ms(t), ms(t + 50))) < early[k] - 30.0) { rt[k] = 2.0 * (t - 400) / 1000.0; break; }
        }
        Check(r[0].Finite() && r[1].Finite() && r[2].Finite(), "output is finite", 1);
        Check(r[2].Peak(0, r[2].l.size()) <= 1.0, "full reverb stays within +-1", r[2].Peak(0, r[2].l.size()));
        Check(Db(rd.Rms(ms(400), ms(700))) < -80.0, "no reverb: the snare is gone by 400 ms", Db(rd.Rms(ms(400), ms(700))), "dB");
        std::printf("       knob 20%% / 50%% / 100%%: level at 400 ms %.1f / %.1f / %.1f dB, measured decay %.2f / %.2f / %.2f s\n",
                    early[0], early[1], early[2], rt[0], rt[1], rt[2]);
        Check(early[0] < early[1] && early[1] < early[2], "more knob is louder", early[2] - early[0], "dB");
        Check(rt[0] > 0 && rt[0] < rt[1] && rt[1] < rt[2], "more knob decays longer (20% vs 100%, s)", rt[2] - rt[0]);
        for (int k = 0; k < 3; k++)
        {
            const double want = Reverb::DecaySeconds(amounts[k]);
            Check(rt[k] > 0.6 * want && rt[k] < 1.3 * want, "measured decay is close to the intended time (s)", rt[k]);
        }
        Check(Db(r[1].Rms(ms(6000), ms(7000))) < -80.0, "50%: silent again after 6 s", Db(r[1].Rms(ms(6000), ms(7000))), "dB");

        double diff = 0;
        for (size_t i = ms(100); i < ms(400); i++) diff = std::fmax(diff, std::fabs(r[1].l[i] - r[1].r[i]));
        Check(diff > 1e-3, "the tail is stereo (left and right differ)", diff);
        double head = 0;
        for (size_t i = hit; i < hit + 100; i++) head = std::fmax(head, std::fabs(r[1].l[i] - rd.l[i]));
        Check(head < 1e-3, "the hit itself is unchanged", head);

        /* Not metallic: a ringing reverb piles its tail into a few
         * resonances. Feed the reverb a single click and compare the
         * strongest 1/12-octave band of the tail with the typical band
         * (median), averaged over six time slices, 300 Hz .. 3 kHz.
         * A smooth room stays within a few dB; a ringing one does not. */
        for (float amount : {0.15f, 0.3f, 0.6f})
        {
            Reverb rv; rv.Init(kSr, NewReverbMem()); rv.Set(amount);
            const size_t n = 48000;
            std::vector<float> ir(n);
            float l, rr;
            for (size_t i = 0; i < 2000; i++) rv.Tick(0.0f, l, rr);    /* let the level settle */
            for (size_t i = 0; i < n; i++) { rv.Tick(i == 0 ? 1.0f : 0.0f, l, rr); ir[i] = l; }
            std::vector<double> mags;
            const size_t win = 3840, first = 1920;                      /* 80 ms slices from 40 ms */
            for (double f = 300.0; f < 3000.0; f *= std::pow(2.0, 1.0 / 12.0))
            {
                double pw = 0;
                for (int sl = 0; sl < 6; sl++)
                {
                    const size_t a = first + sl * win;
                    double re = 0, im = 0, en = 0;
                    for (size_t i = a; i < a + win; i++)
                    {
                        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * (double)(i - a) / (double)win);
                        const double ph = 2.0 * kPi * f * (double)i / kSr;
                        re += w * ir[i] * std::cos(ph); im += w * ir[i] * std::sin(ph);
                        en += (double)ir[i] * ir[i];
                    }
                    pw += (re * re + im * im) / (en > 1e-30 ? en : 1e-30);   /* slice-normalised */
                }
                mags.push_back(pw);
            }
            std::vector<double> sorted = mags;
            std::sort(sorted.begin(), sorted.end());
            const double crest = 10.0 * std::log10(sorted.back() / sorted[sorted.size() / 2]);
            Check(crest < 9.0, "click response: no band rings out above the rest (peak over median)", crest, "dB");
        }

        /* The kick only reaches the reverb when asked to. */
        p.reverb.amount = 0.5f; p.reverb.kick = false;
        Engine k0; k0.Init(kSr, NewReverbMem());
        Run r0 = Render(k0, p, 0, 0, 1.5f, 480, -1);
        p.reverb.kick = true;
        Engine k1; k1.Init(kSr, NewReverbMem());
        Run r1 = Render(k1, p, 0, 0, 1.5f, 480, -1);
        double side0 = 0, side1 = 0;
        for (size_t i = 480; i < r0.l.size(); i++)
        {
            side0 = std::fmax(side0, std::fabs(r0.l[i] - r0.r[i]));
            side1 = std::fmax(side1, std::fabs(r1.l[i] - r1.r[i]));
        }
        Check(side0 == 0.0, "snare-only: a kick stays dry and mono", side0);
        Check(side1 > 1e-3, "kick + snare: the kick gets a tail too", side1);
    }

    /* ── 2. Ducker: Volume ────────────────────────────────────────────── */
    std::printf("Duck: Volume\n");
    {
        Engine e; e.Init(kSr, NewReverbMem());
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
        Engine e2; e2.Init(kSr, NewReverbMem());
        p.duck.depth = 1.0f;
        Run r2 = Render(e2, p, 1000, 0.5f, 0.2f, 480, -1);
        Check(Db(r2.Rms(480 + 480, 480 + 1920)) < -80.0, "depth 100% mutes", Db(r2.Rms(480 + 480, 480 + 1920)), "dB");
        Engine e3; e3.Init(kSr, NewReverbMem());
        p.duck.depth = 0.0f;
        Run r3 = Render(e3, p, 1000, 0.5f, 0.2f, 480, -1);
        Check(std::fabs(Db(r3.Rms(960, 2400) / (0.5 / std::sqrt(2.0)))) < 0.05, "depth 0 leaves the signal alone",
              Db(r3.Rms(960, 2400) / (0.5 / std::sqrt(2.0))), "dB");

        /* Snare amount scales the snare's ducking independently. */
        Engine e4; e4.Init(kSr, NewReverbMem());
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
        p.duck.type = kDuckSplit; p.duck.depth = 0.5f; p.duck.focus = 0.5f; p.duck.width = 0.6f;
        p.duck.attack_s = 0.002f; p.duck.hold_s = 0.3f; p.duck.release_s = 0.2f;
        p.kick.tune_hz = 50.0f;
        const double ref = 0.5 / std::sqrt(2.0);
        auto held = [&](const Params& q, float hz) {
            Engine e; e.Init(kSr, NewReverbMem());
            Run r = Render(e, q, hz, 0.5f, 1.0f, 24000, -1);
            return Db(r.Rms(24000 + 4800, 24000 + 12000) / ref);
        };

        Engine probe; probe.Init(kSr, NewReverbMem()); probe.Set(p);
        const float fc = probe.SplitHz();
        Check(std::fabs(fc - 244.9f) < 2.0f, "Focus at noon centres the band near 245 Hz", fc, "Hz");

        /* At rest the signal is untouched at every frequency. */
        const float freqs[] = {40, 100, 245, 500, 2000, 8000};
        double worst = 0;
        for (float f : freqs)
        {
            Engine e; e.Init(kSr, NewReverbMem());
            Run r = Render(e, p, f, 0.5f, 1.0f, -1, -1);
            worst = std::fmax(worst, std::fabs(Db(r.Rms(24000, 48000) / ref)));
        }
        Check(worst < 0.05, "flat at rest (worst deviation)", worst, "dB");

        const double at_c = held(p, fc);
        Check(std::fabs(at_c + 12.04) < 0.5, "depth 50% cuts the centre by 12 dB, like Volume", at_c, "dB");
        const double far_hi = held(p, 5000.0f), far_lo = held(p, 30.0f);
        Check(std::fabs(far_hi) < 0.5, "4+ octaves above the band passes", far_hi, "dB");
        Check(far_lo > -3.0, "3 octaves below the band barely moves", far_lo, "dB");

        /* Full depth notches the centre right out. */
        Params deep = p; deep.duck.depth = 1.0f;
        const double notch = held(deep, fc);
        Check(notch < -30.0, "depth 100% removes the centre", notch, "dB");

        /* Focus places the band anywhere; it does not follow Kick Tune. */
        Params f0 = p; f0.duck.focus = 0.0f;
        Params f1 = p; f1.duck.focus = 1.0f;
        Params kt = p; kt.kick.tune_hz = 110.0f;
        Engine e0; e0.Init(kSr, NewReverbMem()); e0.Set(f0);
        Engine e1; e1.Init(kSr, NewReverbMem()); e1.Set(f1);
        Engine ek; ek.Init(kSr, NewReverbMem()); ek.Set(kt);
        Check(std::fabs(e0.SplitHz() - 30.0f) < 0.5f && std::fabs(e1.SplitHz() - 2000.0f) < 5.0f,
              "Focus spans 30 Hz to 2 kHz", e1.SplitHz(), "Hz");
        Check(ek.SplitHz() == fc, "retuning the kick leaves the band where it is", ek.SplitHz(), "Hz");
        Check(held(f1, 2000.0f) < -11.0 && held(f1, fc) > -2.0, "Focus right: 2 kHz ducks, 245 Hz does not",
              held(f1, 2000.0f), "dB");

        /* Width works like Q: an octave off centre, wide cuts much more. */
        Params narrow = deep; narrow.duck.width = 0.0f;
        Params wide   = deep; wide.duck.width   = 1.0f;
        const double n_oct = held(narrow, fc * 2.0f), w_oct = held(wide, fc * 2.0f);
        Check(n_oct > -2.0, "narrow: an octave above the centre is nearly untouched", n_oct, "dB");
        Check(w_oct < -6.0, "wide: an octave above the centre is cut too", w_oct, "dB");
    }

    /* ── 4. Ducker: Carve ─────────────────────────────────────────────── */
    std::printf("Duck: Carve\n");
    {
        Params p = Quiet();
        p.duck.type = kDuckCarve; p.duck.depth = 0.5f; p.duck.focus = 0.5f; p.duck.width = 0.6f;
        p.duck.attack_s = 0.002f; p.duck.hold_s = 0.3f; p.duck.release_s = 0.2f;
        p.kick.tune_hz = 50.0f; p.snare.tune_hz = 200.0f; p.duck.snare_amt = 1.0f;
        const double ref = 0.5 / std::sqrt(2.0);

        Engine probe; probe.Init(kSr, NewReverbMem()); probe.Set(p);
        const float fk = probe.KickCarveHz(), fs = probe.SnareCarveHz();
        Check(std::fabs(fk - 70.0f) < 0.5f, "kick cut sits at 1.4x the kick tuning", fk, "Hz");
        Check(std::fabs(fs - 250.0f) < 0.5f, "snare cut sits at 1.25x the snare tuning", fs, "Hz");

        Engine a; a.Init(kSr, NewReverbMem());
        Run ra = Render(a, p, fk, 0.5f, 1.0f, 24000, -1);
        const double at_k = Db(ra.Rms(24000 + 4800, 24000 + 12000) / ref);
        Check(std::fabs(at_k + 12.04) < 0.5, "kick trigger cuts 12 dB at the kick centre", at_k, "dB");
        Check(std::fabs(Db(ra.Rms(4800, 24000) / ref)) < 0.05, "flat at rest", Db(ra.Rms(4800, 24000) / ref), "dB");

        Engine b; b.Init(kSr, NewReverbMem());
        Run rb = Render(b, p, 5000, 0.5f, 1.0f, 24000, -1);
        const double far = Db(rb.Rms(24000 + 4800, 24000 + 12000) / ref);
        Check(std::fabs(far) < 0.3, "5 kHz is left alone", far, "dB");

        Engine c; c.Init(kSr, NewReverbMem());
        Run rc = Render(c, p, fs, 0.5f, 1.0f, -1, 24000);
        const double at_s = Db(rc.Rms(24000 + 4800, 24000 + 12000) / ref);
        Check(std::fabs(at_s + 12.04) < 0.5, "snare trigger cuts 12 dB at the snare centre", at_s, "dB");

        Engine d2; d2.Init(kSr, NewReverbMem());
        Run rd = Render(d2, p, fs, 0.5f, 1.0f, 24000, -1);
        const double kick_on_snare_band = Db(rd.Rms(24000 + 4800, 24000 + 12000) / ref);
        Check(kick_on_snare_band > -4.0, "kick trigger barely touches the snare band", kick_on_snare_band, "dB");
    }

    /* ── 5. Type changes and robustness ───────────────────────────────── */
    std::printf("Robustness\n");
    {
        /* Switching type on a steady tone must not click: the largest
         * sample-to-sample jump stays close to the tone's own slope. */
        Engine e; e.Init(kSr, NewReverbMem());
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
        Engine w; w.Init(kSr, NewReverbMem());
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
