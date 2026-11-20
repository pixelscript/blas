/**
 * @file blas_surface.h
 * @brief Blas's declared surface: pages, knobs, CV depth controls and
 *        Settings layout.
 *
 * Kept apart from blas.cpp (which owns the hardware, audio and gestures)
 * so the host tests can build the very same declarations and check the
 * web-editor descriptor that is derived from them.
 *
 * Include from exactly one translation unit per program.
 */
#pragma once

#include <cmath>
#include <cstdint>

#include "alchemy/hw/alchemy_lab.h"
#include "alchemy/surface/button_bank.h"
#include "alchemy/surface/cv_source.h"
#include "alchemy/surface/jack.h"
#include "alchemy/surface/page.h"
#include "alchemy/surface/presets.h"
#include "alchemy/surface/settings.h"
#include "alchemy/surface/virtual_button.h"
#include "alchemy/surface/virtual_knob.h"

#include "blas_dsp.h"

using namespace alchemy;

/* ═════════════════════════════════════════════════════════════════════════
 *  Layout
 * ═════════════════════════════════════════════════════════════════════════ */

enum : uint8_t { kPageMain = 0, kPageDuck, kPageKick, kPageSnare, kPageRumble, kNumAppPages };
inline constexpr uint8_t kRumbleKnobs = 4;   /* P5 and P6 on the rumble page are free */
enum : uint8_t { kRowDuck = 0, kRowKick, kRowSnare, kNumRows };

/* Jack indices into hw.cv_jacks[] / the ADC's CV channels (0 = J3). */
inline constexpr uint8_t kJackKickTrig  = 0;   /* J3 */
inline constexpr uint8_t kJackSnareTrig = 1;   /* J4 */
inline constexpr uint8_t kJackRowCv[kNumRows] = {2, 3, 4};   /* J5, J6, J7 */
inline constexpr uint8_t kNumAdcJacks   = 5;   /* J3..J7 are read as inputs */

inline constexpr LedPanel::Rgb kRowColor[kNumRows] = {
    {0x90, 0x30, 0xFF},   /* duck: violet  */
    {0xFF, 0x38, 0x00},   /* kick: orange  */
    {0x00, 0xB0, 0xFF},   /* snare: blue   */
};
/* A lighter tint for the right-hand knob of each row on the main page. */
inline constexpr LedPanel::Rgb kRumbleColor = {0xC0, 0x00, 0x18};   /* deep red */
inline constexpr LedPanel::Rgb kRumbleTint  = {0xFF, 0x50, 0x60};
inline constexpr LedPanel::Rgb kRowTint[kNumRows] = {
    {0xC0, 0x80, 0xFF},
    {0xFF, 0x80, 0x30},
    {0x60, 0xD8, 0xFF},
};
inline constexpr LedPanel::Rgb kDim   = {0x18, 0x18, 0x18};
inline constexpr LedPanel::Rgb kWhite = {0xFF, 0xFF, 0xFF};
inline constexpr LedPanel::Rgb kCyan  = {0x00, 0xE0, 0xFF};

inline constexpr float kDepthDetent = 0.08f;   /* +-8% around noon = no CV */

/* ═════════════════════════════════════════════════════════════════════════
 *  Knobs
 * ═════════════════════════════════════════════════════════════════════════ */

/* ── Main page: the six controls you play ─────────────────────────────── */
inline VirtualKnob k_depth = VirtualKnob(kPotTopLeft, "Depth")
    .Linear(0.f, 100.f).Unit("%").Ident("duck.depth")
    .Ring(Level(kRowColor[kRowDuck]));
inline VirtualKnob k_release = VirtualKnob(kPotTopRight, "Release")
    .Exp(20.f, 2000.f).Unit("ms").Ident("duck.release")
    .Ring(Level(kRowTint[kRowDuck]));
inline VirtualKnob k_kick_level = VirtualKnob(kPotMiddleLeft, "Kick Level")
    .Linear(0.f, 100.f).Unit("%").Ident("kick.level")
    .Ring(Level(kRowColor[kRowKick]));
inline VirtualKnob k_kick_tune = VirtualKnob(kPotMiddleRight, "Kick Tune")
    .Exp(30.f, 120.f).Unit("Hz").Ident("kick.tune")
    .Ring(Level(kRowTint[kRowKick]));
inline VirtualKnob k_snare_level = VirtualKnob(kPotBottomLeft, "Snare Level")
    .Linear(0.f, 100.f).Unit("%").Ident("snare.level")
    .Ring(Level(kRowColor[kRowSnare]));
inline VirtualKnob k_snare_tune = VirtualKnob(kPotBottomRight, "Snare Tune")
    .Exp(110.f, 330.f).Unit("Hz").Ident("snare.tune")
    .Ring(Level(kRowTint[kRowSnare]));

/* ── Duck options (tap B1) ────────────────────────────────────────────── */
inline const char* const kDuckTypes[blas::kDuckTypeCount] = {"Volume", "Split", "Carve"};

inline VirtualKnob k_attack = VirtualKnob(kPotTopLeft, "Attack")
    .Exp(0.5f, 100.f).Unit("ms").Ident("duck.attack")
    .Ring(Level(kRowColor[kRowDuck]));
inline VirtualKnob k_hold = VirtualKnob(kPotTopRight, "Hold")
    .Linear(0.f, 250.f).Unit("ms").Ident("duck.hold")
    .Ring(Level(kRowTint[kRowDuck]));
inline VirtualKnob k_curve = VirtualKnob(kPotMiddleLeft, "Curve")
    .Linear(-100.f, 100.f).Unit("%").Ident("duck.curve")
    .Ring(Bipolar(kRowTint[kRowDuck], kRowColor[kRowDuck], kWhite));
inline VirtualKnob k_type = VirtualKnob(kPotMiddleRight, "Type")
    .Selector(blas::kDuckTypeCount).Labels(kDuckTypes).Ident("duck.type")
    .Ring(SelectorRing(kRowColor[kRowDuck], kDim, blas::kDuckTypeCount,
                       ZoneGeometry::Region));
inline VirtualKnob k_focus = VirtualKnob(kPotBottomLeft, "Focus")
    .Linear(0.f, 100.f).Unit("%").Ident("duck.focus")
    .Ring(Bipolar(kRowTint[kRowDuck], kRowColor[kRowDuck], kWhite));
inline VirtualKnob k_width = VirtualKnob(kPotBottomRight, "Width")
    .Linear(0.f, 100.f).Unit("%").Ident("duck.width")
    .Ring(Level(kRowTint[kRowDuck]));

/* ── Kick options (tap B2) ────────────────────────────────────────────── */
inline VirtualKnob k_kick_bend = VirtualKnob(kPotTopLeft, "Kick Bend")
    .Linear(0.f, 100.f).Unit("%").Ident("kick.bend")
    .Ring(Level(kRowColor[kRowKick]));
inline VirtualKnob k_kick_sweep = VirtualKnob(kPotTopRight, "Kick Sweep")
    .Linear(0.f, 100.f).Unit("%").Ident("kick.sweep")
    .Ring(Level(kRowTint[kRowKick]));
inline VirtualKnob k_kick_punch = VirtualKnob(kPotMiddleLeft, "Kick Punch")
    .Linear(0.f, 100.f).Unit("%").Ident("kick.punch")
    .Ring(Level(kRowColor[kRowKick]));
inline VirtualKnob k_kick_click = VirtualKnob(kPotMiddleRight, "Kick Click")
    .Linear(0.f, 100.f).Unit("%").Ident("kick.click")
    .Ring(Level(kRowTint[kRowKick]));
inline VirtualKnob k_kick_drive = VirtualKnob(kPotBottomLeft, "Kick Drive")
    .Linear(0.f, 100.f).Unit("%").Ident("kick.drive")
    .Ring(Level(kRowColor[kRowKick]));
inline VirtualKnob k_kick_decay = VirtualKnob(kPotBottomRight, "Kick Decay")
    .Exp(120.f, 2000.f).Unit("ms").Ident("kick.decay")
    .Ring(Level(kRowTint[kRowKick]));

/* ── Snare options (tap B3) ───────────────────────────────────────────── */
inline VirtualKnob k_snare_decay = VirtualKnob(kPotTopLeft, "Snare Decay")
    .Exp(80.f, 600.f).Unit("ms").Ident("snare.decay")
    .Ring(Level(kRowColor[kRowSnare]));
inline VirtualKnob k_snare_snappy = VirtualKnob(kPotTopRight, "Snare Snappy")
    .Linear(0.f, 100.f).Unit("%").Ident("snare.snappy")
    .Ring(Level(kRowTint[kRowSnare]));
inline VirtualKnob k_snare_tone = VirtualKnob(kPotMiddleLeft, "Snare Tone")
    .Linear(0.f, 100.f).Unit("%").Ident("snare.tone")
    .Ring(Level(kRowColor[kRowSnare]));
inline VirtualKnob k_snare_bend = VirtualKnob(kPotMiddleRight, "Snare Bend")
    .Linear(0.f, 100.f).Unit("%").Ident("snare.bend")
    .Ring(Level(kRowTint[kRowSnare]));
inline VirtualKnob k_snare_drive = VirtualKnob(kPotBottomLeft, "Snare Drive")
    .Linear(0.f, 100.f).Unit("%").Ident("snare.drive")
    .Ring(Level(kRowColor[kRowSnare]));
inline VirtualKnob k_reverb = VirtualKnob(kPotBottomRight, "Reverb")
    .Linear(0.f, 100.f).Unit("%").Ident("snare.reverb")
    .Ring(Level(kWhite));

/* ── Rumble (tap B2 twice) ─────────────────────────────────────────────── */
inline VirtualKnob k_rumble_amount = VirtualKnob(kPotTopLeft, "Rumble")
    .Linear(0.f, 100.f).Unit("%").Ident("rumble.amount")
    .Ring(Level(kRumbleColor));
inline VirtualKnob k_rumble_tone = VirtualKnob(kPotTopRight, "Rumble Tone")
    .Exp(50.f, 400.f).Unit("Hz").Ident("rumble.tone")
    .Ring(Level(kRumbleTint));
inline VirtualKnob k_rumble_decay = VirtualKnob(kPotMiddleLeft, "Rumble Decay")
    .Exp(0.4f, 4.f).Unit("s").Ident("rumble.decay")
    .Ring(Level(kRumbleColor));
inline VirtualKnob k_rumble_drive = VirtualKnob(kPotMiddleRight, "Rumble Drive")
    .Linear(0.f, 100.f).Unit("%").Ident("rumble.drive")
    .Ring(Level(kRumbleTint));

inline Page page_main = Page(kPageMain).Name("Blas").Color("#ffffff")
    .Knobs(k_depth, k_release, k_kick_level, k_kick_tune, k_snare_level, k_snare_tune);
inline Page page_duck = Page(kPageDuck).Name("Duck").Color(kRowColor[kRowDuck])
    .Knobs(k_attack, k_hold, k_curve, k_type, k_focus, k_width);
inline Page page_kick = Page(kPageKick).Name("Kick").Color(kRowColor[kRowKick])
    .Knobs(k_kick_bend, k_kick_sweep, k_kick_punch, k_kick_click, k_kick_drive, k_kick_decay);
inline Page page_snare = Page(kPageSnare).Name("Snare").Color(kRowColor[kRowSnare])
    .Knobs(k_snare_decay, k_snare_snappy, k_snare_tone, k_snare_bend, k_snare_drive, k_reverb);
inline Page page_rumble = Page(kPageRumble).Name("Rumble").Color(kRumbleColor)
    .Knobs(k_rumble_amount, k_rumble_tone, k_rumble_decay, k_rumble_drive)
    .HidePot(kPotBottomLeft).HidePot(kPotBottomRight);

/** The row a page belongs to (whose button lights for it), or -1 (main). */
inline int PageRow(uint8_t page)
{
    switch (page)
    {
        case kPageDuck:   return kRowDuck;
        case kPageKick:   return kRowKick;
        case kPageRumble: return kRowKick;
        case kPageSnare:  return kRowSnare;
        default:          return -1;
    }
}

/** Where a tap on a row's button goes from @p page: through that row's
 *  pages in turn (the kick has two: Kick, then Rumble), then back to main. */
inline uint8_t NextPageOnTap(uint8_t row, uint8_t page)
{
    static constexpr uint8_t kDuckPages[]  = {kPageDuck};
    static constexpr uint8_t kKickPages[]  = {kPageKick, kPageRumble};
    static constexpr uint8_t kSnarePages[] = {kPageSnare};
    const uint8_t* list = kDuckPages;
    uint8_t        n    = 1;
    if (row == kRowKick)       { list = kKickPages;  n = 2; }
    else if (row == kRowSnare) { list = kSnarePages; n = 1; }
    for (uint8_t i = 0; i < n; i++)
        if (list[i] == page) return (i + 1u < n) ? list[i + 1] : static_cast<uint8_t>(kPageMain);
    return list[0];
}

/* ═════════════════════════════════════════════════════════════════════════
 *  Row CV: one jack per row, a depth per knob
 *
 *  J5 can move any duck knob, J6 any kick knob, J7 any snare knob: the two
 *  on the main page plus the six on the row's options page. Each knob has
 *  its own depth (an attenuverter, -1..+1). At full depth +-5 V moves the
 *  knob half its travel.
 *
 *  The button to hold is the knob's own row button (RowOf): B1 for duck
 *  knobs, B2 for kick knobs, B3 for snare knobs. On a row's options page
 *  that one button therefore reaches all six knobs.
 *
 *  The depths are Settings controls, so they persist, appear in the
 *  Settings menu and the web editor, and the hold-and-turn gesture simply
 *  writes the same value. The CV itself is sampled in the audio callback
 *  so a pitch CV that changes with a trigger is already there for the hit.
 * ═════════════════════════════════════════════════════════════════════════ */

inline int RowOf(uint8_t page, uint8_t pot)
{
    if (page == kPageMain) return (pot < 2u * kNumRows) ? static_cast<int>(pot / 2u) : -1;
    if (page == kPageRumble) return (pot < kRumbleKnobs) ? static_cast<int>(kRowKick) : -1;
    if (page < kNumAppPages) return static_cast<int>(page) - 1;
    return -1;
}

inline float ApplyDetent(float d)
{
    const float a = std::fabs(d);
    if (a <= kDepthDetent) return 0.0f;
    const float s = (a - kDepthDetent) / (1.0f - kDepthDetent);
    return d < 0.0f ? -s : s;
}

class RowCv : public CvSource
{
  public:
    float DeltaAtPage(uint8_t page, uint8_t pot) const override
    {
        if (page >= kNumAppPages || pot >= kNumPots) return 0.0f;
        const int row = RowOf(page, pot);
        return row < 0 ? 0.0f : depth_[page][pot] * cv_[row];
    }

    void Update(const float* /*cv*/, uint32_t /*t_ms*/) override
    {
        for (uint8_t pg = 0; pg < kNumAppPages; pg++)
            for (uint8_t p = 0; p < kNumPots; p++)
                depth_[pg][p] = ApplyDetent(handle[pg][p].Value());
    }

    /** Bipolar CV for @p row: -0.5..+0.5 for -5..+5 V. Audio thread. */
    void SetCv(uint8_t row, float bipolar) { cv_[row] = bipolar; }

    float Depth(uint8_t page, uint8_t pot) const { return depth_[page][pot]; }

    BipolarHandle handle[kNumAppPages][kNumPots];

  private:
    float          depth_[kNumAppPages][kNumPots] = {};
    volatile float cv_[kNumRows]                  = {};
};

/* Names for the CV depth controls (Settings menu pages + web editor). */
struct CvSlotName { const char* ident; const char* name; };
inline const CvSlotName kCvSlots[kNumAppPages][kNumPots] = {
    {{"cv.duck.depth", "J5 > Depth"},        {"cv.duck.release", "J5 > Release"},
     {"cv.kick.level", "J6 > Kick Level"},   {"cv.kick.tune", "J6 > Kick Tune"},
     {"cv.snare.level", "J7 > Snare Level"}, {"cv.snare.tune", "J7 > Snare Tune"}},
    {{"cv.duck.attack", "J5 > Attack"},      {"cv.duck.hold", "J5 > Hold"},
     {"cv.duck.curve", "J5 > Curve"},        {"cv.duck.type", "J5 > Type"},
     {"cv.duck.focus", "J5 > Focus"},        {"cv.duck.width", "J5 > Width"}},
    {{"cv.kick.bend", "J6 > Bend"},          {"cv.kick.sweep", "J6 > Sweep"},
     {"cv.kick.punch", "J6 > Punch"},        {"cv.kick.click", "J6 > Click"},
     {"cv.kick.drive", "J6 > Drive"},        {"cv.kick.decay", "J6 > Decay"}},
    {{"cv.snare.decay", "J7 > Decay"},       {"cv.snare.snappy", "J7 > Snappy"},
     {"cv.snare.tone", "J7 > Tone"},         {"cv.snare.bend", "J7 > Bend"},
     {"cv.snare.drive", "J7 > Drive"},       {"cv.snare.reverb", "J7 > Reverb"}},
    {{"cv.rumble.amount", "J6 > Rumble"},    {"cv.rumble.tone", "J6 > Rumble Tone"},
     {"cv.rumble.decay", "J6 > Rumble Decay"}, {"cv.rumble.drive", "J6 > Rumble Drive"},
     {nullptr, nullptr},                     {nullptr, nullptr}},
};
inline const char* const kCvPageNames[kNumAppPages] = {
    "CV: Main", "CV: Duck (J5)", "CV: Kick (J6)", "CV: Snare (J7)", "CV: Rumble (J6)"};

inline const char* const kOffOn[2]    = {"Off", "On"};
inline const char* const kEnvModes[3] = {"Dip", "Bump", "Full"};
inline const char* const kReverbOn[2] = {"Snare", "Kick + Snare"};
enum : uint8_t { kEnvDip = 0, kEnvBump, kEnvFull };

/* Jack labels for the web editor. */
inline constexpr Jack kJacks[10] = {
    Jack("j1",  "Audio In L",        JackSig::AudioIn),
    Jack("j2",  "Audio In R",        JackSig::AudioIn),
    Jack("j3",  "Kick Trigger",      JackSig::Trig),
    Jack("j4",  "Snare Trigger",     JackSig::Trig),
    Jack("j5",  "Duck CV",           JackSig::CvBi),
    Jack("j6",  "Kick CV",           JackSig::CvBi),
    Jack("j7",  "Snare CV",          JackSig::CvBi),
    Jack("j8",  "Duck Envelope Out", JackSig::CvUni),
    Jack("j9",  "Audio Out L",       JackSig::AudioOut),
    Jack("j10", "Audio Out R",       JackSig::AudioOut),
};

/* ═════════════════════════════════════════════════════════════════════════
 *  Drum models
 *
 *  Chosen with combos (hold B2 + tap B3: next kick, hold B3 + tap B2: next
 *  snare). Stored as host-only button state, so they are saved in presets,
 *  restored at power-on and editable in the web editor. The button LED
 *  takes the model's colour.
 * ═════════════════════════════════════════════════════════════════════════ */

inline const char* const kKickModels[blas::kKickModelCount]   = {"909", "808", "FM"};
inline const char* const kSnareModels[blas::kSnareModelCount] = {"909", "808", "Clap"};

inline constexpr LedPanel::Rgb kKickModelColor[blas::kKickModelCount] = {
    {0xFF, 0x38, 0x00},   /* 909: orange (the kick row's colour) */
    {0xFF, 0xB0, 0x00},   /* 808: amber                          */
    {0xFF, 0x00, 0x60},   /* FM:  hot pink                       */
};
inline constexpr LedPanel::Rgb kSnareModelColor[blas::kSnareModelCount] = {
    {0x00, 0xB0, 0xFF},   /* 909:  blue (the snare row's colour) */
    {0x00, 0xFF, 0x90},   /* 808:  teal                          */
    {0x70, 0xFF, 0x20},   /* Clap: green                         */
};

inline VirtualButton b_kick_model  = VirtualButton("kick.model", "Kick Model").Selector(kKickModels);
inline VirtualButton b_snare_model = VirtualButton("snare.model", "Snare Model").Selector(kSnareModels);

/* Rumble on/off: hold B2, tap B1. Off closes the send, so the tail rings out. */
inline VirtualButton b_rumble_on = VirtualButton("rumble.on", "Rumble On").Selector(kOffOn).Default(1);

/** What blas.cpp needs back from the Settings declaration. */
struct SettingsRefs
{
    SelectorHandle accent;   /* trigger height sets the hit's level */
    SelectorHandle env;      /* what J8 puts out                    */
    SelectorHandle reverb;   /* which drums feed the reverb         */
    uint16_t       cv_offset[kNumAppPages][kNumPots] = {};   /* blob offsets */
};

inline void DeclareSettings(Settings& settings, Presets& presets, RowCv& row_cv,
                            SettingsRefs& refs)
{
    /* Page 1: the basics. */
    settings.UseBrightness();               /* P1 */
    settings.UsePresets(presets);           /* P3 slot, P4 save / load */
    refs.accent = settings.Page(0).Pot(kPotTopRight).Selector(kOffOn)
        .Default(1).Ident("trig.accent").Name("Trigger accent")
        .Help("On: a trigger's voltage sets how hard the drum hits (1.5 V soft, "
              "4.5 V and up full). Off: every hit is full level, with no added delay.");
    refs.env = settings.Page(0).Pot(kPotBottomLeft).Selector(kEnvModes)
        .Default(kEnvDip).Ident("env.mode").Name("J8 envelope")
        .Help("Dip: 5 V at rest, falling by the Depth amount. Bump: 0 V at rest, "
              "rising by the Depth amount. Full: 0 to 5 V whatever Depth is set to.");
    refs.reverb = settings.Page(0).Pot(kPotBottomRight).Selector(kReverbOn)
        .Default(0).Ident("rev.source").Name("Reverb on")
        .Help("Which drums the Reverb knob (snare page, P6) sends to the reverb.");
    settings.Page(0).Name("Setup");

    /* Pages 2-6: CV depth for every knob, laid out like the knob pages. */
    for (uint8_t pg = 0; pg < kNumAppPages; pg++)
    {
        const uint8_t spage = static_cast<uint8_t>(1u + pg);
        settings.Page(spage).Name(kCvPageNames[pg]);
        for (uint8_t p = 0; p < kNumPots; p++)
        {
            const int row = RowOf(pg, p);
            if (row < 0) continue;            /* no knob there */
            row_cv.handle[pg][p] = settings.Page(spage).Pot(p).Bipolar()
                .Color(pg == kPageRumble ? kRumbleColor : kRowColor[row]).CenterColor(kWhite)
                .Ident(kCvSlots[pg][p].ident).Name(kCvSlots[pg][p].name);
        }
    }

    /* Where each depth sits in the Settings blob (for the hold gesture). */
    uint16_t at = 0;
    for (uint8_t spage = 0; spage < settings.NumPages(); spage++)
        for (uint8_t p = 0; p < kNumPots; p++)
        {
            if (spage >= 1u && spage <= kNumAppPages)
                refs.cv_offset[spage - 1u][p] = at;
            at = static_cast<uint16_t>(
                at + Settings::PersistedBytesFor(settings.KindAt(spage, p)));
        }
}

