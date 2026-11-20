/**
 * Host-side checks for the two framework behaviours Blas leans on:
 *
 *   1. HoldPager: while a row button is held the pots must not move any
 *      parameter, and afterwards the moved knob must be picked up again
 *      without a jump.
 *   2. Settings: a CV depth written through SetSerializedField (the hold
 *      gesture) reads back from its handle and survives a save / load.
 *   3. The web-editor descriptor derived from blas_surface.h builds
 *      cleanly, so the module is editable from the browser.
 *
 * Builds against the SDK's host stubs:  make test
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <string>

#include "alchemy/hw/alchemy_lab.h"
#include "alchemy/version.h"
#include "alchemy/host_link/describe.h"
#include "alchemy/host_link/descriptor.h"
#include "alchemy/surface/settings.h"
#include "blas_surface.h"
#include "hold_pager.h"
#include "row_gestures.h"

using namespace alchemy;

static int g_fail = 0;
static void Check(bool ok, const char* what, double got)
{
    std::printf("  %s %-62s %8.4f\n", ok ? "ok  " : "FAIL", what, got);
    if (!ok) g_fail++;
}

int main()
{
    std::printf("HoldPager\n");
    {
        HoldPager pager(4, kNumPots);
        float phys[kNumPots] = {0.30f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f};
        uint32_t t = 0;
        auto frames = [&](int n) { for (int i = 0; i < n; i++) { t += 16; pager.Update(phys, t); } };

        frames(4);
        /* Turn pot 0 to 0.40: caught, so the value follows. */
        for (float v = 0.30f; v <= 0.401f; v += 0.01f) { phys[0] = v; frames(1); }
        Check(std::fabs(pager.Stored(0, 0) - 0.40f) < 0.01f, "a caught pot moves its parameter", pager.Stored(0, 0));
        const float before = pager.Stored(0, 0);

        /* Hold: sweep the pot to 0.90. Nothing may change. */
        pager.Freeze(phys);
        for (float v = 0.40f; v <= 0.901f; v += 0.02f) { phys[0] = v; frames(1); }
        Check(pager.Stored(0, 0) == before, "frozen: sweeping the pot leaves the parameter alone", pager.Stored(0, 0));

        /* Release: still nothing, and the pot is waiting to be picked up. */
        pager.Thaw();
        frames(3);
        Check(pager.Stored(0, 0) == before, "after release the parameter has not jumped", pager.Stored(0, 0));
        Check(!pager.Caught(0), "the moved pot is detached until it is turned back", pager.Caught(0));
        Check(pager.Caught(1), "pots that did not move stay attached", pager.Caught(1));

        /* Wiggling short of the stored value does nothing... */
        for (float v = 0.90f; v >= 0.60f; v -= 0.02f) { phys[0] = v; frames(1); }
        Check(pager.Stored(0, 0) == before, "turning back, short of the value: still unchanged", pager.Stored(0, 0));
        /* ...and crossing it picks the pot up again. */
        for (float v = 0.60f; v >= 0.20f; v -= 0.02f) { phys[0] = v; frames(1); }
        Check(pager.Caught(0) && std::fabs(pager.Stored(0, 0) - 0.20f) < 0.03f,
              "crossing the value picks the pot up again", pager.Stored(0, 0));

        /* A tap (freeze, no motion, thaw) changes nothing at all. */
        const float v1 = pager.Stored(0, 1);
        pager.Freeze(phys); frames(5); pager.Thaw(); frames(3);
        Check(pager.Stored(0, 1) == v1 && pager.Caught(1), "a tap leaves values and catch untouched", pager.Stored(0, 1));

        /* Jumping to another page while values differ re-arms catch. */
        pager.SetStored(2, 3, 0.8f, phys);
        pager.GoToPage(2, phys);
        frames(2);
        Check(pager.Page() == 2 && std::fabs(pager.Stored(2, 3) - 0.8f) < 1e-6f && !pager.Caught(3),
              "page jump keeps the stored value until the pot reaches it", pager.Stored(2, 3));
    }

    std::printf("Settings-backed CV depths\n");
    static AlchemyLab       hw;
    static HoldPager        pager(kNumAppPages, kNumPots);
    static Settings         settings(hw, &pager);
    static daisy::QSPIHandle qspi;
    static Presets          presets(qspi);
    static RowCv            row_cv;
    static SettingsRefs     refs;
    {
        /* The firmware's own declaration (blas_surface.h). */
        DeclareSettings(settings, presets, row_cv, refs);
        auto& h = row_cv.handle;

        Check(settings.NumPages() == 6, "six settings pages (setup + five CV pages)", settings.NumPages());
        Check(refs.cv_offset[kPageRumble][kRumbleKnobs - 1] + 4u == settings.SerializedSize(),
              "the last depth is the last field of the settings blob", settings.SerializedSize());

        const float phys[kNumPots] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        bool all_zero = true;
        for (auto& row : h) for (auto& x : row) all_zero = all_zero && x.Value() == 0.0f;
        Check(all_zero, "every depth starts at zero", 0);

        /* The gesture: write pot position 0.75 into (Kick page, pot 4). */
        float v = 0.75f;
        const bool ok = settings.SetSerializedField(refs.cv_offset[kPageKick][3],
                                                    reinterpret_cast<const uint8_t*>(&v), 4, phys);
        Check(ok && std::fabs(h[kPageKick][3].Value() - 0.5f) < 1e-6f,
              "writing 0.75 gives depth +0.5 on that knob", h[kPageKick][3].Value());
        v = 0.0f;
        settings.SetSerializedField(refs.cv_offset[kPageMain][0], reinterpret_cast<const uint8_t*>(&v), 4, phys);
        Check(std::fabs(h[kPageMain][0].Value() + 1.0f) < 1e-6f, "writing 0.0 gives depth -1 (fully inverted)",
              h[kPageMain][0].Value());
        int others = 0;
        for (uint8_t pg = 0; pg < kNumAppPages; pg++)
            for (uint8_t p = 0; p < kNumPots; p++)
                if (h[pg][p].Value() != 0.0f) others++;
        Check(others == 2, "no other depth was touched", others);
        Check(refs.accent.Value() == 1 && refs.env.Value() == kEnvDip, "the setup selectors were not disturbed",
              refs.accent.Value());

        /* Save, wipe, load. */
        std::vector<uint8_t> blob(settings.SerializedSize());
        settings.Serialize(blob.data());
        v = 0.5f;
        settings.SetSerializedField(refs.cv_offset[kPageKick][3], reinterpret_cast<const uint8_t*>(&v), 4, phys);
        settings.Deserialize(blob.data());
        Check(std::fabs(h[kPageKick][3].Value() - 0.5f) < 1e-6f, "depths survive a save and load",
              h[kPageKick][3].Value());

        /* Routing: each jack reaches its own row's knobs and no others. */
        row_cv.Update(nullptr, 0);
        row_cv.SetCv(kRowDuck, 0.5f); row_cv.SetCv(kRowKick, 0.5f); row_cv.SetCv(kRowSnare, 0.5f);
        Check(std::fabs(row_cv.DeltaAtPage(kPageMain, 0) + 0.5f) < 1e-6f,
              "+5 V at full inverted depth moves the knob down half its travel", row_cv.DeltaAtPage(kPageMain, 0));
        row_cv.SetCv(kRowDuck, 0.0f); row_cv.SetCv(kRowSnare, 0.0f);
        Check(row_cv.DeltaAtPage(kPageMain, 0) == 0.0f, "J6 alone does not reach a duck knob", row_cv.DeltaAtPage(kPageMain, 0));
        Check(row_cv.DeltaAtPage(kPageKick, 3) > 0.2f, "J6 reaches the kick knob that has depth", row_cv.DeltaAtPage(kPageKick, 3));
        Check(RowOf(kPageMain, 2) == kRowKick && RowOf(kPageMain, 5) == kRowSnare && RowOf(kPageDuck, 4) == kRowDuck,
              "rows: main page pairs, then one page per row", RowOf(kPageMain, 2));
        Check(ApplyDetent(0.05f) == 0.0f && ApplyDetent(-0.05f) == 0.0f && std::fabs(ApplyDetent(1.0f) - 1.0f) < 1e-6f,
              "noon detent: small depths read as off, full stays full", ApplyDetent(0.05f));
    }

    std::printf("Button gestures (played through the real pager and settings)\n");
    {
        /* A miniature of ControlLoop::Tick: 16 button polls, then the pot
         * snapshot, CV refresh, pager update and the frame hook. */
        static RowGestures g;
        float    pots[kNumPots] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.30f};
        float    phys[kNumPots] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.30f};
        bool     down[kNumRows] = {false, false, false};
        bool     menu = false;
        uint32_t t = 100000;
        int      combos = 0, last_held = -1, last_tap = -1;
        auto frame = [&]() {
            for (int i = 0; i < 16; i++) g.Poll(down, menu, t + i, phys, pager);
            t += 16;
            std::memcpy(phys, pots, sizeof phys);
            row_cv.Update(nullptr, t);
            if (menu) return;
            pager.Update(phys, t);
            const RowGestures::Actions a = g.Frame(phys, pager.Page());
            if (a.combo_held >= 0) { combos++; last_held = a.combo_held; last_tap = a.combo_tap; }
            if (a.nav_row >= 0)
            {
                pager.GoToPage(NextPageOnTap(static_cast<uint8_t>(a.nav_row), pager.Page()), phys);
            }
            if (a.depth_pot >= 0)
                settings.SetSerializedField(refs.cv_offset[pager.Page()][a.depth_pot],
                                            reinterpret_cast<const uint8_t*>(&a.depth_pos), 4, phys);
        };
        auto frames = [&](int n) { for (int i = 0; i < n; i++) frame(); };
        auto tap    = [&](int b) { down[b] = true; frames(5); down[b] = false; frames(3); };
        auto sweep  = [&](int p, float to) {
            const float from = pots[p];
            for (int i = 1; i <= 20; i++) { pots[p] = from + (to - from) * (float)i / 20.0f; frame(); }
        };
        auto zero_depths = [&]() {
            float half = 0.5f;
            for (uint8_t pg = 0; pg < kNumAppPages; pg++)
                for (uint8_t p = 0; p < kNumPots; p++)
                    if (RowOf(pg, p) >= 0)
                        settings.SetSerializedField(refs.cv_offset[pg][p], reinterpret_cast<const uint8_t*>(&half), 4, phys);
        };

        pager.GoToPage(kPageMain, phys);
        frames(4);
        zero_depths();

        /* Main page: hold B1, turn P1. */
        const float depth_before = pager.Stored(kPageMain, 0);
        down[kRowDuck] = true; frames(3);
        sweep(0, 0.90f);
        Check(g.ClaimedPot() == 0, "main page: holding B1 and turning P1 claims P1", g.ClaimedPot());
        down[kRowDuck] = false; frames(3);
        Check(row_cv.Depth(kPageMain, 0) > 0.7f, "main page: its CV depth is set", row_cv.Depth(kPageMain, 0));
        Check(pager.Stored(kPageMain, 0) == depth_before && pager.Page() == kPageMain,
              "main page: Depth itself did not move, and no page change", pager.Stored(kPageMain, 0));

        /* A row button does not reach another row's knob on the main page. */
        down[kRowDuck] = true; frames(3);
        sweep(2, 0.80f);
        Check(g.ClaimedPot() < 0 && row_cv.Depth(kPageMain, 2) == 0.0f,
              "main page: B1 does not set a kick knob's depth", row_cv.Depth(kPageMain, 2));
        down[kRowDuck] = false; frames(3);

        /* Tap B2: the kick options page. */
        tap(kRowKick);
        Check(pager.Page() == kPageKick, "tapping B2 opens the kick page", pager.Page());

        /* Kick page: B2 reaches all six knobs; the other buttons none. */
        const float bend_before = pager.Stored(kPageKick, 0);
        down[kRowDuck] = true; frames(30);
        sweep(0, 0.60f);
        Check(g.ClaimedPot() < 0 && row_cv.Depth(kPageKick, 0) == 0.0f,
              "kick page: B1 does not set a kick knob", g.ClaimedPot());
        down[kRowDuck] = false; frames(3);
        Check(pager.Page() == kPageKick, "kick page: and a long B1 hold does not change page", pager.Page());
        down[kRowKick] = true; frames(3);
        sweep(0, 0.10f);
        Check(g.ClaimedPot() == 0, "kick page: holding B2 and turning P1 claims P1", g.ClaimedPot());
        down[kRowKick] = false; frames(3);
        Check(row_cv.Depth(kPageKick, 0) < -0.7f, "kick page: its CV depth is set (inverted)", row_cv.Depth(kPageKick, 0));
        Check(pager.Stored(kPageKick, 0) == bend_before && pager.Page() == kPageKick,
              "kick page: Bend itself did not move, still on the kick page", pager.Stored(kPageKick, 0));
        row_cv.SetCv(kRowKick, 0.25f);
        Check(row_cv.DeltaAtPage(kPageKick, 0) < -0.15f, "kick page: +2.5 V on J6 now pulls Bend down",
              row_cv.DeltaAtPage(kPageKick, 0));

        /* Every knob of every options page, with that page's own button. */
        int set = 0;
        for (uint8_t row = 0; row < kNumRows; row++)
        {
            const uint8_t page = static_cast<uint8_t>(1 + row);
            if (pager.Page() != page) tap(row);
            for (uint8_t p = 0; p < kNumPots; p++)
            {
                zero_depths(); frames(1);
                down[row] = true; frames(3);
                sweep(p, pots[p] > 0.5f ? 0.05f : 0.95f);
                down[row] = false; frames(3);
                if (pager.Page() == page && std::fabs(row_cv.Depth(page, p)) > 0.7f) set++;
            }
        }
        Check(set == 18, "all 18 options-page knobs take a depth from their page's button", set);

        /* Tap again: back to the main page. Tap another row: straight there. */
        tap(kRowSnare);
        Check(pager.Page() == kPageMain, "tapping the open page's button returns to the main page", pager.Page());
        tap(kRowDuck); tap(kRowKick);
        Check(pager.Page() == kPageKick, "tapping another row's button jumps to that row's page", pager.Page());

        /* The kick has a second page: tap B2 again for Rumble. */
        tap(kRowKick);
        Check(pager.Page() == kPageRumble, "tapping B2 on the kick page opens the rumble page", pager.Page());
        zero_depths(); frames(1);
        const float amount_before = pager.Stored(kPageRumble, 0);
        down[kRowKick] = true; frames(3);
        sweep(0, pots[0] > 0.5f ? 0.05f : 0.95f);
        down[kRowKick] = false; frames(3);
        Check(std::fabs(row_cv.Depth(kPageRumble, 0)) > 0.7f && pager.Stored(kPageRumble, 0) == amount_before
              && pager.Page() == kPageRumble,
              "rumble page: hold B2 + turn P1 sets J6's depth on Rumble, Rumble itself unmoved",
              row_cv.Depth(kPageRumble, 0));
        row_cv.SetCv(kRowKick, 0.25f);
        Check(std::fabs(row_cv.DeltaAtPage(kPageRumble, 0)) > 0.15f, "rumble page: J6 then moves Rumble",
              row_cv.DeltaAtPage(kPageRumble, 0));
        row_cv.SetCv(kRowKick, 0.0f);
        down[kRowKick] = true; frames(3);
        sweep(4, pots[4] > 0.5f ? 0.05f : 0.95f);
        Check(g.ClaimedPot() < 0, "rumble page: P5 has no knob, so nothing is claimed", g.ClaimedPot());
        down[kRowKick] = false; frames(3);
        Check(pager.Page() == kPageRumble, "a quick press that turned any knob is not a tap", pager.Page());
        int rumble_set = 0;
        for (uint8_t p = 0; p < kRumbleKnobs; p++)
        {
            zero_depths(); frames(1);
            down[kRowKick] = true; frames(3);
            sweep(p, pots[p] > 0.5f ? 0.05f : 0.95f);
            down[kRowKick] = false; frames(3);
            if (std::fabs(row_cv.Depth(kPageRumble, p)) > 0.7f) rumble_set++;
        }
        Check(rumble_set == 4, "rumble page: all four knobs take a depth from B2", rumble_set);
        zero_depths(); frames(1);
        down[kRowDuck] = true; frames(30);
        sweep(1, pots[1] > 0.5f ? 0.05f : 0.95f);
        down[kRowDuck] = false; frames(3);
        Check(row_cv.Depth(kPageRumble, 1) == 0.0f && pager.Page() == kPageRumble,
              "rumble page: B1 sets nothing there", row_cv.Depth(kPageRumble, 1));
        tap(kRowKick);
        Check(pager.Page() == kPageMain, "tapping B2 on the rumble page returns to the main page", pager.Page());
        tap(kRowSnare); tap(kRowKick);
        Check(pager.Page() == kPageKick, "from another row's page, B2 lands on the kick page first", pager.Page());
        tap(kRowKick); tap(kRowKick);

        /* A long press with nothing turned does nothing. */
        down[kRowKick] = true; frames(40); down[kRowKick] = false; frames(3);
        Check(pager.Page() == kPageMain, "a long press without turning a knob changes nothing", pager.Page());

        /* The Settings chord and the press that closes the menu are not taps. */
        down[kRowKick] = true; frames(2); down[kRowSnare] = true; frames(130);
        menu = true; frames(2);
        down[kRowKick] = false; down[kRowSnare] = false; frames(3);
        Check(pager.Page() == kPageMain && g.HeldRow() < 0, "B2+B3 (open Settings) is not a tap or a hold", pager.Page());
        down[kRowKick] = true; g.Poll(down, true, t, phys, pager);
        menu = false; frames(4);                 /* the menu closes on this press */
        down[kRowKick] = false; frames(3);
        Check(pager.Page() == kPageMain, "the press that closes Settings does not open a page", pager.Page());

        /* Combos: hold one button, tap another. */
        combos = 0;
        down[kRowKick] = true; frames(3);
        tap(kRowSnare);
        Check(combos == 1 && last_held == kRowKick && last_tap == kRowSnare,
              "hold B2, tap B3: one combo (B2 held, B3 tapped)", combos);
        tap(kRowSnare); tap(kRowSnare);
        Check(combos == 3, "keep B2 held and tap B3 twice more: two more", combos);
        down[kRowKick] = false; frames(3);
        Check(pager.Page() == kPageMain && combos == 3, "letting go of B2 afterwards is not a tap", pager.Page());

        combos = 0;
        down[kRowSnare] = true; frames(3);
        tap(kRowKick);
        down[kRowSnare] = false; frames(3);
        Check(combos == 1 && last_held == kRowSnare && last_tap == kRowKick,
              "hold B3, tap B2: the other way round", combos);

        combos = 0;
        down[kRowDuck] = true; frames(3);
        tap(kRowKick);
        down[kRowDuck] = false; frames(3);
        Check(combos == 1 && last_held == kRowDuck && last_tap == kRowKick && pager.Page() == kPageMain,
              "hold B1, tap B2: reported, no page change", combos);

        /* Not combos: a slow second press, or the Settings chord. */
        combos = 0;
        down[kRowKick] = true; frames(3);
        down[kRowSnare] = true; frames(40); down[kRowSnare] = false; frames(3);
        down[kRowKick] = false; frames(3);
        Check(combos == 0, "B3 held for 0.6 s while B2 is down: not a combo", combos);
        down[kRowKick] = true; frames(2); down[kRowSnare] = true; frames(130);
        menu = true; frames(2);
        down[kRowKick] = false; down[kRowSnare] = false; frames(3);
        menu = false; frames(3);
        Check(combos == 0 && pager.Page() == kPageMain, "B2+B3 held into Settings: not a combo", combos);

        /* A hold that set a depth, then a combo, still keeps the depth. */
        combos = 0;
        tap(kRowKick);                                   /* to the kick page */
        down[kRowKick] = true; frames(3);
        tap(kRowSnare);
        Check(combos == 1 && pager.Page() == kPageKick, "combos work on an options page too", combos);
        down[kRowKick] = false; frames(3);
        Check(pager.Page() == kPageKick, "and do not navigate", pager.Page());
        tap(kRowKick); tap(kRowKick);
        Check(pager.Page() == kPageMain, "(back to main)", pager.Page());
    }

    std::printf("Web editor descriptor\n");
    {
        presets.Manage(pager);
        presets.Manage(settings);
        static ButtonBank bank;
        bank.Global(b_kick_model).Global(b_snare_model).Global(b_rumble_on);
        presets.Manage(bank);
        presets.UseNames();

        const Page*   page_refs[kNumAppPages] = {&page_main, &page_duck, &page_kick, &page_snare, &page_rumble};
        const hostlink::PageSet pages{page_refs, kNumAppPages};
        const hostlink::DescriptorBuilder::ModuleInfo info =
            {"blas", "Blas", "0.0.0", "test", ALCHEMY_SDK_VERSION, "v2"};

        const Jack* jack_refs[10];
        for (int i = 0; i < 10; i++) jack_refs[i] = &kJacks[i];

        static char buf[hostlink::kDefaultDescriptorCapacity];
        bool good = false;
        const uint32_t len = hostlink::RenderDescriptor(buf, sizeof buf, info, presets, &pages, nullptr, 0,
                                                        nullptr, 0, nullptr, 0, jack_refs, 10, nullptr,
                                                        nullptr, 0, &good);
        const std::string json(buf, len);
        Check(good && len > 0, "the descriptor builds without errors", len);
        Check(len < sizeof buf / 2, "and uses under half of the SDK's descriptor buffer (bytes)", len);
        const char* want[] = {
            "\"id\":\"duck.depth\"", "\"id\":\"duck.type\"", "\"Volume\",\"Split\",\"Carve\"",
            "\"id\":\"kick.punch\"", "\"id\":\"kick.sweep\"", "\"id\":\"cv.kick.sweep\"", "\"id\":\"snare.snappy\"", "\"id\":\"snare.reverb\"", "\"id\":\"rev.source\"", "\"id\":\"cv.kick.tune\"",
            "\"id\":\"cv.snare.reverb\"", "\"id\":\"snare.bend\"", "\"id\":\"trig.accent\"", "\"id\":\"env.mode\"",
            "\"Blas\",\"Duck\",\"Kick\",\"Snare\",\"Rumble\"", "CV: Kick (J6)", "Duck Envelope Out",
            "\"id\":\"rumble.amount\"", "\"id\":\"rumble.tone\"", "\"id\":\"rumble.decay\"",
            "\"id\":\"rumble.drive\"", "\"id\":\"cv.rumble.amount\"", "\"id\":\"rumble.on\"", "CV: Rumble (J6)",
            "\"id\":\"kick.model\"", "\"id\":\"snare.model\"", "\"909\",\"808\",\"FM\"", "\"909\",\"808\",\"Clap\""};
        int found = 0;
        for (const char* w : want)
        {
            if (json.find(w) != std::string::npos) found++;
            else std::printf("       missing: %s\n", w);
        }
        Check(found == (int)(sizeof want / sizeof want[0]), "pages, knobs, CV depths and settings are all described", found);
        Check(presets.FitsInSlot() && presets.LiveSize() <= 512u, "the state fits a preset slot and the autosave buffer (bytes)",
              presets.LiveSize());
        /* The model choice: SetZone (the combo) reads back, and survives a
         * save and load of the whole preset. */
        bank.SetZone(b_kick_model, 2);
        bank.SetZone(b_snare_model, 1);
        bank.Update(0);
        Check(b_kick_model.Zone() == 2 && b_snare_model.Zone() == 1, "model combos read back (FM kick, 808 snare)",
              b_kick_model.Zone());
        std::vector<uint8_t> live(presets.LiveSize());
        presets.SerializeLive(live.data(), live.size());
        bank.SetZone(b_kick_model, 0);
        bank.SetZone(b_snare_model, 0);
        bank.Update(0);
        const bool loaded = presets.DeserializeLive(live.data(), live.size());
        bank.Update(0);
        Check(loaded && b_kick_model.Zone() == 2 && b_snare_model.Zone() == 1, "and come back with a preset",
              b_kick_model.Zone());
        if (!good) std::printf("%.*s\n", (int)(len < 600 ? len : 600), buf);
    }

    std::printf(g_fail ? "\n%d check(s) FAILED\n" : "\nAll checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
