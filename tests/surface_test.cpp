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

        Check(settings.NumPages() == 5, "five settings pages (setup + four CV pages)", settings.NumPages());
        Check(refs.cv_offset[kPageSnare][kNumPots - 1] + 4u == settings.SerializedSize(),
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

    std::printf("Web editor descriptor\n");
    {
        presets.Manage(pager);
        presets.Manage(settings);
        presets.UseNames();

        const Page*   page_refs[kNumAppPages] = {&page_main, &page_duck, &page_kick, &page_snare};
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
            "\"id\":\"kick.punch\"", "\"id\":\"snare.snappy\"", "\"id\":\"cv.kick.tune\"",
            "\"id\":\"cv.snare.duck\"", "\"id\":\"trig.accent\"", "\"id\":\"env.mode\"",
            "\"Blas\",\"Duck\",\"Kick\",\"Snare\"", "CV: Kick (J6)", "Duck Envelope Out"};
        int found = 0;
        for (const char* w : want)
        {
            if (json.find(w) != std::string::npos) found++;
            else std::printf("       missing: %s\n", w);
        }
        Check(found == (int)(sizeof want / sizeof want[0]), "pages, knobs, CV depths and settings are all described", found);
        Check(presets.FitsInSlot() && presets.LiveSize() <= 512u, "the state fits a preset slot and the autosave buffer (bytes)",
              presets.LiveSize());
        if (!good) std::printf("%.*s\n", (int)(len < 600 ? len : 600), buf);
    }

    std::printf(g_fail ? "\n%d check(s) FAILED\n" : "\nAll checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
