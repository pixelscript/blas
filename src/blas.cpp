/**
 * blas.cpp — Blas: a side-chain ducker that brings its own kick and snare,
 * for the Hermetic Modular Alchemy Lab (V2).
 *
 * Stereo audio in on J1/J2 is ducked by triggers on J3 (kick) and J4
 * (snare). Each trigger also plays a 909-style drum, mixed in after the
 * ducker and sent with the audio to J9/J10. See README.md for the manual.
 *
 * Layout, three rows as on Athanor:
 *
 *            LEFT            BUTTON   RIGHT          CV in
 *   Row 1    [P1] Depth      B1       [P2] Release   J5   duck
 *   Row 2    [P3] Kick Level B2       [P4] Kick Tune J6   kick
 *   Row 3    [P5] Snare Lvl  B3       [P6] Snare Tune J7  snare
 *                                       triggers: J3 kick, J4 snare
 *                                       envelope out: J8
 *
 *   Tap a row's button      open that row's six-knob options page
 *                           (tap again to return)
 *   Hold it + turn a knob   set how much the row's CV jack moves that knob
 *                           (noon = off, right = positive, left = inverted).
 *                           Main page: the row's two knobs. On the row's
 *                           own options page: any of the six.
 *
 * Built on the Alchemy SDK framework: Pager (pages, pot-catch), Settings
 * (the CV depths live there, so they are also editable from the Settings
 * menu and the web editor), Presets and HostLink. The audio engine is
 * blas_dsp.{h,cpp}, which has no SDK dependency.
 */

#include <cmath>
#include <cstring>

#include "daisy_seed.h"
#include "alchemy/hw/alchemy_lab.h"
#include "alchemy/version.h"
#include "alchemy/control/tick_timebase.h"
#include "alchemy/host_link/diagnostics.h"
#include "alchemy/host_link/host.h"
#include "alchemy/led/animations.h"
#include "alchemy/midi/types.h"
#include "alchemy/midi/usb_transport.h"
#include "alchemy/surface/control_loop.h"
#include "alchemy/surface/cv_source.h"
#include "alchemy/surface/page.h"
#include "alchemy/surface/pager.h"
#include "alchemy/surface/presets.h"
#include "alchemy/surface/settings.h"
#include "alchemy/surface/virtual_knob.h"

#include "blas_dsp.h"
#include "blas_surface.h"
#include "hold_pager.h"
#include "row_gestures.h"

#ifndef BLAS_VERSION
#define BLAS_VERSION "0.0.0"
#endif
#ifndef BLAS_GIT_HASH
#define BLAS_GIT_HASH "dev"
#endif

using namespace alchemy;

/* Trigger thresholds (volts at the jack). */
static constexpr float kTrigHiV = 1.5f;
static constexpr float kTrigLoV = 0.8f;

/* Gestures. */
static constexpr uint32_t kPeekMs = 250u;    /* hold: show the CV depths */

/* ═════════════════════════════════════════════════════════════════════════
 *  Meters (written by the audio callback, read by the LEDs)
 * ═════════════════════════════════════════════════════════════════════════ */

static volatile float meter_gain  = 1.0f;   /* ducker gain, 1 = open       */
static volatile float meter_shape = 0.0f;   /* duck envelope, 0..1         */
static volatile float meter_kick  = 0.0f;
static volatile float meter_snare = 0.0f;

/* Depth ring: a white pip rides the live gain reduction, jumping out to
 * the Depth setting on each hit and sliding home as the signal recovers. */
static void DrawDuckMeter(LedPanel& panel, uint8_t pot, const ArcGeometry& geo,
                          float norm, uint32_t /*t_ms*/, void* /*ctx*/)
{
    const float shape = meter_shape;
    if (shape < 0.02f) return;
    const float pos  = norm * shape;
    float       hour = geo.start_hour
                     + geo.step_hours * static_cast<float>(geo.arc_leds - 1u) * pos;
    while (hour >= 12.0f) hour -= 12.0f;
    panel.SetRingByHour(pot, hour, panel.ScaleGlobal(kWhite));
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Surfaces
 * ═════════════════════════════════════════════════════════════════════════ */

static AlchemyLab  hw;
static ControlLoop loop    (hw);
static HoldPager   pager   (kNumAppPages, kNumPots);
static Presets     presets (hw.seed.qspi);
static Settings    settings(hw, &pager);
static RowCv       row_cv;
static ButtonBank  buttons;        /* the drum models (host-only state) */

static SettingsRefs refs;           /* setup selectors + CV depth offsets  */

static hostlink::Diagnostics    debug;
static hostlink::Host           host(presets, "blas", "Blas", BLAS_VERSION, BLAS_GIT_HASH);
static hostlink::Gauge<float>   g_gain("duck.gain", "Duck gain");
static hostlink::Gauge<float>   g_cpu("cpu.load", "CPU load");

static blas::Engine engine;
/* Reverb and rumble delay lines live in external SDRAM (too big for the fast internal
 * RAM the rest of the engine uses). Cleared by engine.Init(). */
static float DSY_SDRAM_BSS reverb_mem[blas::Engine::kMemFloats];

/* ═════════════════════════════════════════════════════════════════════════
 *  Audio
 * ═════════════════════════════════════════════════════════════════════════ */

/* Raw ADC words for J3..J7. The SDK's own jack readings are filtered and
 * refreshed by the control loop every millisecond; triggers and pitch CV
 * want less latency than that, so the audio callback reads the converter
 * directly and applies the board's calibration record itself (the same
 * maths as the SDK's CvInput: inverting input stage, per-jack zero code). */
static uint16_t* jack_adc[kNumAdcJacks]  = {};
static float     jack_zero[kNumAdcJacks] = {};   /* raw code at 0 V        */
static float     jack_scale              = 0.0f; /* volts per raw code     */

static inline float JackVolts(uint8_t j)
{
    return (jack_zero[j] - static_cast<float>(*jack_adc[j])) * jack_scale;
}

struct TrigIn
{
    bool  high    = false;
    bool  pending = false;   /* edge seen; fire next block with its level */
    float edge_v  = 0.0f;
};
static TrigIn trig[2];

/* Hits requested from the control loop (USB MIDI). 0 = none. */
static volatile float midi_kick_vel  = 0.0f;
static volatile float midi_snare_vel = 0.0f;

static volatile bool    accent_on = true;
static volatile uint8_t env_mode  = kEnvDip;
static volatile bool    reverb_kick = false;

static float    row_cv_lp[kNumRows] = {};
static uint32_t cpu_ticks_max       = 0;      /* worst block since last read */
static float    ticks_per_block     = 1.0f;

/** Trigger height -> hit level: 1.5 V is a soft hit, 4.5 V and up is full. */
static inline float AccentVel(float volts)
{
    float t = (volts - kTrigHiV) / (4.5f - kTrigHiV);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return 0.3f + 0.7f * t;
}

/** Returns the velocity of a hit to fire this block, or 0. */
static inline float PollTrigger(TrigIn& t, float volts)
{
    float fire = 0.0f;
    if (t.pending)
    {
        /* One block after the edge the trigger has finished rising. */
        t.pending = false;
        fire = AccentVel(volts > t.edge_v ? volts : t.edge_v);
    }
    if (!t.high && volts > kTrigHiV)
    {
        t.high = true;
        if (accent_on) { t.pending = true; t.edge_v = volts; }
        else           { fire = 1.0f; }
    }
    else if (t.high && volts < kTrigLoV)
    {
        t.high = false;
    }
    return fire;
}

static inline float Ms(const VirtualKnob& k) { return k.Value() * 0.001f; }

static void ReadParams(blas::Params& p)
{
    p.duck.depth     = k_depth.Norm();
    p.duck.release_s = Ms(k_release);
    p.duck.attack_s  = Ms(k_attack);
    p.duck.hold_s    = Ms(k_hold);
    p.duck.curve     = k_curve.Norm();
    p.duck.type      = static_cast<int>(k_type.Value());
    p.duck.focus     = k_focus.Norm();
    p.duck.width     = k_width.Norm();

    p.kick.model   = b_kick_model.Zone();
    p.snare.model  = b_snare_model.Zone();
    p.kick.level   = k_kick_level.Norm();
    p.kick.tune_hz = k_kick_tune.Value();
    p.kick.decay_s = Ms(k_kick_decay);
    p.kick.bend    = k_kick_bend.Norm();
    p.kick.punch   = k_kick_punch.Norm();
    p.kick.click   = k_kick_click.Norm();
    p.kick.drive   = k_kick_drive.Norm();
    p.kick.sweep   = k_kick_sweep.Norm();

    p.snare.level   = k_snare_level.Norm();
    p.snare.tune_hz = k_snare_tune.Value();
    p.snare.decay_s = Ms(k_snare_decay);
    p.snare.snappy  = k_snare_snappy.Norm();
    p.snare.tone    = k_snare_tone.Norm();
    p.snare.bend    = k_snare_bend.Norm();
    p.reverb.amount = k_reverb.Norm();
    p.rumble.on      = b_rumble_on.Zone() != 0u;
    p.rumble.amount  = k_rumble_amount.Norm();
    p.rumble.tone_hz = k_rumble_tone.Value();
    p.rumble.decay_s = k_rumble_decay.Value();
    p.rumble.drive   = k_rumble_drive.Norm();
    p.reverb.kick   = reverb_kick;
    p.snare.drive   = k_snare_drive.Norm();
}

static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           n)
{
    const uint32_t t0 = daisy::System::GetTick();

    /* Jacks: J3..J7 volts, straight from the converter. */
    float volts[kNumAdcJacks] = {};
    if (jack_adc[0])
    {
        for (uint8_t j = 0; j < kNumAdcJacks; j++) volts[j] = JackVolts(j);
    }

    /* Row CV first, so a pitch that steps with its trigger lands on the hit. */
    for (uint8_t r = 0; r < kNumRows; r++)
    {
        float v = volts[kJackRowCv[r]];
        v = v < -5.5f ? -5.5f : (v > 5.5f ? 5.5f : v);
        row_cv_lp[r] += 0.3f * (v - row_cv_lp[r]);
        float vv = row_cv_lp[r];
        if (vv < 0.04f && vv > -0.04f) vv = 0.0f;   /* unpatched jack noise */
        row_cv.SetCv(r, vv * 0.1f);
    }

    blas::Params p;
    ReadParams(p);
    engine.Set(p);

    /* Triggers: jacks, then anything the control loop asked for (MIDI). */
    float kick_vel  = PollTrigger(trig[0], volts[kJackKickTrig]);
    float snare_vel = PollTrigger(trig[1], volts[kJackSnareTrig]);
    const float mk = midi_kick_vel, ms = midi_snare_vel;
    if (mk > 0.0f) { midi_kick_vel = 0.0f;  if (mk > kick_vel)  kick_vel  = mk; }
    if (ms > 0.0f) { midi_snare_vel = 0.0f; if (ms > snare_vel) snare_vel = ms; }
    if (kick_vel > 0.0f)  engine.TriggerKick(kick_vel);
    if (snare_vel > 0.0f) engine.TriggerSnare(snare_vel);

    engine.Process(in[0], in[1], out[0], out[1], n);

    /* J8: the duck envelope, for pumping other modules. */
    const float gain  = engine.DuckGain();
    const float shape = engine.DuckShape();
    float env_v;
    switch (env_mode)
    {
        case kEnvBump: env_v = 5.0f * (1.0f - gain); break;   /* 0 V, rises by depth  */
        case kEnvFull: env_v = 5.0f * shape;          break;  /* 0..5 V, any depth    */
        default:       env_v = 5.0f * gain;           break;  /* 5 V, dips by depth   */
    }
    hw.j8.SetVolts(env_v);

    meter_gain  = gain;
    meter_shape = shape;
    meter_kick  = engine.KickEnv();
    meter_snare = engine.SnareEnv();

    const uint32_t dt = daisy::System::GetTick() - t0;
    if (dt > cpu_ticks_max) cpu_ticks_max = dt;
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Buttons: tap = options page, hold + turn = CV depth
 * ═════════════════════════════════════════════════════════════════════════ */

static RowGestures gestures;
static uint32_t    now_ms = 0;

/* ── USB MIDI: notes fire the drums ───────────────────────────────────── */
static midi::DaisyUsbPort<> midi_port;
static TickTimebase         midi_time;
static bool                 midi_ready = false;

static bool OnMidiEvent(const midi::Event& e)
{
    if (e.type != midi::EventType::NoteOn || e.value == 0) return true;
    const float vel = 0.3f + 0.7f * static_cast<float>(e.value > 127 ? 127 : e.value) / 127.0f;
    switch (e.data1)
    {
        case 35: case 36: midi_kick_vel  = vel; break;   /* B0 / C1: kick  */
        case 38: case 40: midi_snare_vel = vel; break;   /* D1 / E1: snare */
        default: break;
    }
    return true;
}

static void OnPoll(uint32_t t_ms)
{
    now_ms = t_ms;

    if (midi_time.Initialized()) midi_time.Update(daisy::System::GetTick());
    if (midi_ready) midi_port.Poll(midi_time.GetSample(), OnMidiEvent);

    bool down[kNumRows];
    for (uint8_t i = 0; i < kNumRows; i++) down[i] = hw.buttons[i].Pressed();
    gestures.Poll(down, settings.IsActive(), t_ms, loop.Phys(), pager);
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Frame (60 Hz, skipped while the Settings menu is open)
 * ═════════════════════════════════════════════════════════════════════════ */

static void WriteDepth(uint8_t page, uint8_t pot, float pot_norm)
{
    /* The Settings control stores the pot position (noon = 0); writing it
     * through the settings path keeps the menu, presets and web editor in
     * step with the gesture. */
    settings.SetSerializedField(refs.cv_offset[page][pot],
                                reinterpret_cast<const uint8_t*>(&pot_norm),
                                sizeof(float), loop.Phys());
}

/* The live state is kept in the first preset slot (the boot slot): a few seconds
 * after the last change it is written back, so the module powers up as
 * you left it. Loading another preset becomes the live state the same way. */
static constexpr uint32_t kAutosaveQuietMs = 3000u;
static uint32_t state_hash    = 0;
static uint32_t saved_hash    = 0;
static uint32_t state_since   = 0;
static bool     autosave_ok   = false;
static uint8_t  hash_buf[512];

static uint32_t LiveHash()
{
    const size_t len = presets.SerializeLive(hash_buf, sizeof(hash_buf));
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= hash_buf[i]; h *= 16777619u; }
    return h ^ static_cast<uint32_t>(len);
}

static void Autosave(uint32_t t_ms)
{
    static uint32_t last_check = 0;
    if (!autosave_ok || t_ms - last_check < 250u) return;
    last_check = t_ms;

    const uint32_t h = LiveHash();
    if (h != state_hash) { state_hash = h; state_since = t_ms; return; }
    if (h == saved_hash || gestures.HeldRow() >= 0) return;
    if (t_ms - state_since < kAutosaveQuietMs) return;
    /* The preset area is shared with other Alchemy firmwares. If the first
     * slot holds another firmware's preset, leave it alone: saving a Blas
     * preset there from the Settings menu adopts the slot. */
    const Presets::SlotInfo info = presets.InfoFor(0);
    if (info.valid && !info.schema_match) return;
    if (presets.Save(0)) saved_hash = h;
    else                 autosave_ok = false;   /* don't hammer a failing flash */
}

/* After a model change, that row's two main rings show which model is on. */
static constexpr uint32_t kShowModelMs  = 1200u;
static int8_t             show_model_row = -1;
static uint32_t           show_model_ms  = 0;
static bool               show_rumble    = false;   /* after the on/off combo */
static uint32_t           show_rumble_ms = 0;

static void OnFrame()
{
    const float* phys = loop.Phys();

    const RowGestures::Actions act = gestures.Frame(phys, pager.Page());

    /* Tap: toggle between the main page and the tapped row's options. */
    if (act.nav_row >= 0)
    {
        pager.GoToPage(NextPageOnTap(static_cast<uint8_t>(act.nav_row), pager.Page()), phys);
    }
    /* Hold one button, tap another. Hold B2 + tap B3: next kick model.
     * Hold B3 + tap B2: next snare model. Hold B2 + tap B1: rumble on/off.
     * The other three are free. */
    if (act.combo_held == kRowKick && act.combo_tap == kRowSnare)
    {
        buttons.SetZone(b_kick_model, static_cast<uint8_t>((b_kick_model.Zone() + 1u) % blas::kKickModelCount));
        show_model_row = kRowKick;
        show_model_ms  = now_ms;
    }
    else if (act.combo_held == kRowSnare && act.combo_tap == kRowKick)
    {
        buttons.SetZone(b_snare_model, static_cast<uint8_t>((b_snare_model.Zone() + 1u) % blas::kSnareModelCount));
        show_model_row = kRowSnare;
        show_model_ms  = now_ms;
    }
    else if (act.combo_held == kRowKick && act.combo_tap == kRowDuck)
    {
        buttons.SetZone(b_rumble_on, b_rumble_on.Zone() ? 0u : 1u);
        show_rumble_ms = now_ms;
        show_rumble    = true;
    }

    /* Hold + turn: the knob's position becomes its CV depth. */
    if (act.depth_pot >= 0)
        WriteDepth(pager.Page(), static_cast<uint8_t>(act.depth_pot), act.depth_pos);

    /* Settings that the audio callback reads. */
    accent_on = refs.accent.Value() != 0u;
    env_mode  = refs.env.Value();
    reverb_kick = refs.reverb.Value() != 0u;

    Autosave(now_ms);

    /* Diagnostics. */
    static uint32_t last_diag = 0;
    if (now_ms - last_diag >= 250u)
    {
        last_diag = now_ms;
        const uint32_t worst = cpu_ticks_max;
        cpu_ticks_max = 0;
        g_cpu.Set(100.0f * static_cast<float>(worst) / ticks_per_block);
        g_gain.Set(meter_gain);
    }
}

/* ═════════════════════════════════════════════════════════════════════════
 *  LEDs
 * ═════════════════════════════════════════════════════════════════════════ */

static void DrawDepthRing(uint8_t pot, float depth, bool active)
{
    FillDesc desc;
    desc.mode          = FillMode::Center;
    desc.color         = LedPanel::Scale(kWhite, active ? 1.0f : 0.35f);   /* positive */
    desc.neg_color     = LedPanel::Scale(kCyan,  active ? 1.0f : 0.35f);   /* inverted */
    desc.center_color  = LedPanel::Scale(kWhite, active ? 0.6f : 0.25f);   /* noon     */
    desc.passive_color = {0, 0, 0};
    hw.leds.ClearRing(pot);
    DrawFill(hw.leds, pot, hw.Arc(), depth, desc);
}

/** The current model as a row of zones around the ring: lit = selected. */
static void DrawModelRing(uint8_t pot, uint8_t zone, uint8_t zones, LedPanel::Rgb c)
{
    const ArcGeometry& geo = hw.Arc();
    hw.leds.ClearRing(pot);
    const uint8_t per = static_cast<uint8_t>(geo.arc_leds / zones);
    for (uint8_t z = 0; z < zones; z++)
    {
        const LedPanel::Rgb col = (z == zone) ? c : LedPanel::Scale(kWhite, 0.08f);
        for (uint8_t k = 0; k + 1u < per; k++)   /* a gap between zones */
        {
            float hour = geo.start_hour + geo.step_hours * static_cast<float>(z * per + k);
            while (hour >= 12.0f) hour -= 12.0f;
            while (hour < 0.0f)   hour += 12.0f;
            hw.leds.SetRingByHour(pot, hour, hw.leds.ScaleGlobal(col));
        }
    }
}

static inline LedPanel::Rgb RowColorNow(uint8_t row)
{
    if (row == kRowKick)  return kKickModelColor[b_kick_model.Zone() % blas::kKickModelCount];
    if (row == kRowSnare) return kSnareModelColor[b_snare_model.Zone() % blas::kSnareModelCount];
    return kRowColor[row];
}

static void OnRender(uint32_t t_ms)
{
    if (settings.IsActive()) return;

    const uint8_t page        = pager.Page();
    const int8_t  held_row    = gestures.HeldRow();
    const int8_t  claimed_pot = gestures.ClaimedPot();

    /* Buttons: each wears its row's colour; the open page's button is
     * bright. B1 dims with the ducking; B2 and B3 flash on their drum. */
    for (uint8_t i = 0; i < kNumButtons; i++)
    {
        const int row_of_page = PageRow(page);
        float k = (row_of_page < 0) ? 0.35f : (row_of_page == static_cast<int>(i) ? 1.0f : 0.06f);
        LedPanel::Rgb c = (page == kPageRumble && i == kRowKick) ? kRumbleColor : RowColorNow(i);
        if (i == kRowDuck)
        {
            k *= 0.25f + 0.75f * meter_gain;
        }
        else
        {
            const float env = (i == kRowKick) ? meter_kick : meter_snare;
            if (env > 0.25f)
            {
                c = LedPanel::Mix(c, kWhite, 0.6f);
                if (k < 0.8f) k = 0.8f;
            }
        }
        if (held_row == static_cast<int8_t>(i)) { c = kWhite; k = 0.8f; }
        hw.leds.SetButtonPair(i, hw.leds.ScaleGlobal(LedPanel::Scale(c, k)));
    }

    /* Rumble on/off: the kick's two rings show off (left) or on (right). */
    if (show_rumble && (t_ms - show_rumble_ms) < kShowModelMs)
    {
        const uint8_t on = b_rumble_on.Zone() ? 1u : 0u;
        DrawModelRing(2u, on, 2u, kRumbleColor);
        DrawModelRing(3u, on, 2u, kRumbleColor);
    }

    /* Model change: the row's two main-page rings show the model. */
    if (show_model_row >= 0 && (t_ms - show_model_ms) < kShowModelMs)
    {
        const bool    kick  = (show_model_row == kRowKick);
        const uint8_t zone  = kick ? b_kick_model.Zone() : b_snare_model.Zone();
        const uint8_t zones = kick ? static_cast<uint8_t>(blas::kKickModelCount)
                                   : static_cast<uint8_t>(blas::kSnareModelCount);
        const uint8_t pot   = static_cast<uint8_t>(2 * show_model_row);
        DrawModelRing(pot,      zone, zones, RowColorNow(static_cast<uint8_t>(show_model_row)));
        DrawModelRing(pot + 1u, zone, zones, RowColorNow(static_cast<uint8_t>(show_model_row)));
    }

    /* Hold: after a moment the row's rings show their CV depths (white =
     * positive, cyan = inverted, from noon); the one being set is bright. */
    if (held_row >= 0)
    {
        const bool peek = (t_ms - gestures.HeldSince()) >= kPeekMs;
        for (uint8_t p = 0; p < kNumPots; p++)
        {
            if (RowOf(page, p) != held_row) continue;
            const bool mine = (claimed_pot == static_cast<int8_t>(p));
            if (!mine && (!peek || claimed_pot >= 0)) continue;
            DrawDepthRing(p, row_cv.Depth(page, p), mine);
        }
    }
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Startup
 * ═════════════════════════════════════════════════════════════════════════ */

/** Factory value for a knob, given in the knob's own units. Used when
 *  there is no saved state; the pots pick the values up as you turn them. */
static void Seed(uint8_t page, const VirtualKnob& k, float value)
{
    float n;
    const float lo = k.XformMin(), hi = k.XformMax();
    switch (k.Transform())
    {
        case VirtualKnob::Xform::Exp:
            n = std::log(value / lo) / std::log(hi / lo);
            break;
        case VirtualKnob::Xform::Selector:
            n = (value + 0.5f) / hi;                   /* zone centre */
            break;
        default:
            n = (value - lo) / (hi - lo);
            break;
    }
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    pager.SetStored(page, k.Pot(), n, loop.Phys());
}

static void SeedDefaults()
{
    const blas::Params d;   /* the engine's own defaults */

    Seed(kPageMain, k_depth,       d.duck.depth * 100.f);
    Seed(kPageMain, k_release,     d.duck.release_s * 1000.f);
    Seed(kPageMain, k_kick_level,  d.kick.level * 100.f);
    Seed(kPageMain, k_kick_tune,   d.kick.tune_hz);
    Seed(kPageMain, k_snare_level, d.snare.level * 100.f);
    Seed(kPageMain, k_snare_tune,  d.snare.tune_hz);

    Seed(kPageDuck, k_attack, d.duck.attack_s * 1000.f);
    Seed(kPageDuck, k_hold,   d.duck.hold_s * 1000.f);
    Seed(kPageDuck, k_curve,  (d.duck.curve - 0.5f) * 200.f);
    Seed(kPageDuck, k_type,   static_cast<float>(d.duck.type));
    Seed(kPageDuck, k_focus,  d.duck.focus * 100.f);
    Seed(kPageDuck, k_width,  d.duck.width * 100.f);

    Seed(kPageKick, k_kick_decay, d.kick.decay_s * 1000.f);
    Seed(kPageKick, k_kick_bend,  d.kick.bend * 100.f);
    Seed(kPageKick, k_kick_punch, d.kick.punch * 100.f);
    Seed(kPageKick, k_kick_click, d.kick.click * 100.f);
    Seed(kPageKick, k_kick_drive, d.kick.drive * 100.f);
    Seed(kPageKick, k_kick_sweep, d.kick.sweep * 100.f);

    Seed(kPageSnare, k_snare_decay,  d.snare.decay_s * 1000.f);
    Seed(kPageSnare, k_snare_snappy, d.snare.snappy * 100.f);
    Seed(kPageSnare, k_snare_tone,   d.snare.tone * 100.f);
    Seed(kPageSnare, k_snare_bend,   d.snare.bend * 100.f);
    Seed(kPageSnare, k_snare_drive,  d.snare.drive * 100.f);
    Seed(kPageSnare, k_reverb,       d.reverb.amount * 100.f);

    Seed(kPageRumble, k_rumble_amount, d.rumble.amount * 100.f);
    Seed(kPageRumble, k_rumble_tone,   d.rumble.tone_hz);
    Seed(kPageRumble, k_rumble_decay,  d.rumble.decay_s);
    Seed(kPageRumble, k_rumble_drive,  d.rumble.drive * 100.f);
}

int main()
{
    hw.Init();
    const float sr = hw.SampleRate();
    engine.Init(sr, reverb_mem);

    /* J8 is the envelope output; J3..J7 stay inputs. */
    hw.j8.EnableCvOutput();
    hw.j8.SetVolts(5.0f);

    /* Calibrated when the board has a record, design-nominal otherwise. */
    const V2Calibration& cal = hw.Calibration();
    jack_scale = cal.vdda_at_cal / (65535.0f * kV2CvInGainDesign);
    for (uint8_t j = 0; j < kNumAdcJacks; j++)
    {
        jack_zero[j] = static_cast<float>(cal.jack[j].adc_zero_code);
        jack_adc[j]  = hw.seed.adc.GetPtr(static_cast<uint8_t>(kCvAdcOffset + j));
    }

    pager.SetPageColor(kPageMain,  kWhite);
    pager.SetPageColor(kPageDuck,  kRowColor[kRowDuck]);
    pager.SetPageColor(kPageKick,  kRowColor[kRowKick]);
    pager.SetPageColor(kPageSnare, kRowColor[kRowSnare]);
    pager.SetPageColor(kPageRumble, kRumbleColor);

    k_depth.Overdraw(DrawDuckMeter);
    DeclareSettings(settings, presets, row_cv, refs);
    SeedDefaults();

    g_cpu.Unit("%");
    debug.Watch(g_cpu);
    debug.Watch(g_gain);
    host.Extend(debug);
    host.Jacks(kJacks);

    /* USB MIDI shares the composite device with HostLink. If it cannot
     * start, everything else carries on without it. */
    if (daisy::System::GetTickFreq() != 0u)
    {
        midi_time.Init(daisy::System::GetTickFreq(), daisy::System::GetTick());
        usb::Device::Shared().Configure(daisy::UsbHandle::FS_EXTERNAL, "Blas");
        midi_ready = midi_port.Init({}, {0, 1});
        ticks_per_block = static_cast<float>(daisy::System::GetTickFreq())
                        * static_cast<float>(hw.BlockSize()) / sr;
    }

    /* Preset payload. The first slot doubles as the live state (see Autosave). */
    presets.Manage(pager);
    presets.Manage(settings);
    buttons.Global(b_kick_model).Global(b_snare_model).Global(b_rumble_on);
    presets.Manage(buttons);
    host.Fast(pager, settings);
    presets.UseNames();

    loop.Use(pager)
        .Use(settings)
        .Use(row_cv)
        .Use(page_main)
        .Use(page_duck)
        .Use(page_kick)
        .Use(page_snare)
        .Use(page_rumble)
        .Use(buttons)
        .Use(host)
        .OnPoll(OnPoll)
        .OnFrame(OnFrame)
        .OnRender(OnRender);

    presets.Init();
    presets.BootLoad();

    autosave_ok = presets.FitsInSlot() && presets.LiveSize() <= sizeof(hash_buf);
    state_hash  = saved_hash = autosave_ok ? LiveHash() : 0u;

    debug.Info("Blas " BLAS_VERSION " (" BLAS_GIT_HASH "), Alchemy SDK " ALCHEMY_SDK_VERSION);
    if (!midi_ready)  debug.Warn("USB MIDI did not start");
    if (!autosave_ok) debug.Warn("State too large to autosave");

    /* One frame first, so the knobs are wired before audio reads them. */
    loop.Tick();
    hw.StartAudio(AudioCallback);

    for (;;) loop.Tick();
}
