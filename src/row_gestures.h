/**
 * @file row_gestures.h
 * @brief The three row buttons: tap = open the row's options page,
 *        hold + turn one of the row's knobs = set that knob's CV depth.
 *
 * A button reaches the knobs that belong to its row: on the main page its
 * own pair, on its own options page all six. B1 sets how much J5 moves a
 * duck knob, B2 how much J6 moves a kick knob, B3 how much J7 moves a
 * snare knob. On another row's options page a button has no knobs to set.
 *
 * Hold one button and tap another: a combo, reported as (held, tapped).
 * Holding on and tapping again repeats it. The held button's release then
 * does nothing. A combo needs a quick tap, so holding B2 + B3 for two
 * seconds still opens the Settings menu.
 *
 * Pure logic over a HoldPager, so the host tests can play button and knob
 * sequences through it. blas.cpp feeds it the buttons every millisecond
 * (Poll) and the pots every frame (Frame), and carries out what it asks.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "blas_surface.h"
#include "hold_pager.h"

class RowGestures
{
  public:
    static constexpr uint32_t kTapMs       = 400u;    /* longer presses are holds */
    static constexpr float    kClaimTravel = 0.02f;   /* pot travel that claims   */

    /** What the frame should carry out. */
    struct Actions
    {
        int8_t nav_row   = -1;     /* >= 0: toggle this row's options page    */
        int8_t combo_held = -1;    /* >= 0: this button was held ...          */
        int8_t combo_tap  = -1;    /*       ... while this one was tapped     */
        int8_t depth_pot = -1;     /* >= 0: write depth_pos to this pot's depth */
        float  depth_pos = 0.5f;   /* pot position, 0..1 (noon = no CV)       */
    };

    /**
     * Button edges, every millisecond.
     * @param down   Debounced state of B1..B3.
     * @param gated  True while the Settings menu owns the panel.
     * @param phys   The pots as of the last frame.
     */
    void Poll(const bool down[kNumRows], bool gated, uint32_t t_ms,
              const float* phys, HoldPager& pager)
    {
        uint8_t num_down = 0;
        for (uint8_t i = 0; i < kNumRows; i++)
            if (down[i]) num_down++;

        /* A press that begins in the menu, or is the one that closes it,
         * belongs to the menu. */
        const bool menu = gated || was_gated_;
        was_gated_ = gated;

        for (uint8_t i = 0; i < kNumRows; i++)
        {
            Btn& b = btn_[i];
            const bool rising  = down[i] && !b.prev;
            const bool falling = !down[i] && b.prev;
            b.prev = down[i];

            if (rising)
            {
                b.down_ms  = t_ms;
                b.poisoned = menu;
                if (num_down > 1)
                {
                    /* A second button: the start of a combo (or of the
                     * Settings chord). Nothing else may come of either
                     * press. With all three down, nothing at all. */
                    for (uint8_t j = 0; j < kNumRows; j++)
                        if (down[j]) btn_[j].poisoned = true;
                    if (held_row_ >= 0) End(pager);
                    combo_tap_ = -1;
                    if (num_down == 2 && !menu)
                    {
                        for (uint8_t j = 0; j < kNumRows; j++)
                            if (j != i && down[j]) combo_held_ = static_cast<int8_t>(j);
                        combo_tap_ = static_cast<int8_t>(i);
                    }
                }
                else if (!menu)
                {
                    held_row_    = static_cast<int8_t>(i);
                    claimed_pot_ = -1;
                    moved_       = false;
                    held_since_  = t_ms;
                    last_pos_    = -1.0f;
                    std::memcpy(press_phys_, phys, sizeof(press_phys_));
                    pager.Freeze(phys);
                }
            }

            if (falling && combo_tap_ == static_cast<int8_t>(i))
            {
                const bool quick = (t_ms - b.down_ms) < kTapMs;
                if (quick && !menu && combo_held_ >= 0 && down[combo_held_])
                {
                    combo_req_held_ = combo_held_;
                    combo_req_tap_  = combo_tap_;
                }
                combo_tap_ = -1;
            }

            if (falling)
            {
                if (held_row_ == static_cast<int8_t>(i) && !b.poisoned)
                {
                    /* A tap: quick, and no knob moved while it was down. */
                    const bool tap = !moved_ && (t_ms - b.down_ms) < kTapMs;
                    End(pager);
                    if (tap) nav_request_ = static_cast<int8_t>(i);
                }
                b.poisoned = false;
            }
        }

        if (gated && held_row_ >= 0) End(pager);
        if (gated) combo_tap_ = -1;
    }

    /** Once per frame, after the pager has updated. */
    Actions Frame(const float* phys, uint8_t page)
    {
        Actions a;
        a.nav_row    = nav_request_;
        nav_request_ = -1;
        a.combo_held    = combo_req_held_;
        a.combo_tap     = combo_req_tap_;
        combo_req_held_ = combo_req_tap_ = -1;

        if (held_row_ >= 0)
        {
            /* Any knob moving means this press is not a tap. */
            for (uint8_t p = 0; p < kNumPots && !moved_; p++)
                if (std::fabs(phys[p] - press_phys_[p]) > kClaimTravel) moved_ = true;

            /* The first of the held row's knobs to move is the one being
             * set. */
            if (claimed_pot_ < 0)
            {
                for (uint8_t p = 0; p < kNumPots; p++)
                {
                    if (RowOf(page, p) != held_row_) continue;
                    if (std::fabs(phys[p] - press_phys_[p]) > kClaimTravel)
                    {
                        claimed_pot_ = static_cast<int8_t>(p);
                        break;
                    }
                }
            }
            if (claimed_pot_ >= 0)
            {
                float pos = phys[claimed_pot_];
                pos = pos < 0.0f ? 0.0f : (pos > 1.0f ? 1.0f : pos);
                if (last_pos_ < 0.0f || std::fabs(pos - last_pos_) >= 0.002f)
                {
                    last_pos_   = pos;
                    a.depth_pot = claimed_pot_;
                    a.depth_pos = pos;
                }
            }
        }
        return a;
    }

    int8_t   HeldRow()    const { return held_row_; }
    int8_t   ClaimedPot() const { return claimed_pot_; }
    uint32_t HeldSince()  const { return held_since_; }

  private:
    struct Btn
    {
        bool     prev     = false;
        bool     poisoned = false;   /* this press does nothing until released */
        uint32_t down_ms  = 0;
    };

    void End(HoldPager& pager)
    {
        held_row_    = -1;
        claimed_pot_ = -1;
        pager.Thaw();
    }

    Btn      btn_[kNumRows];
    bool     was_gated_   = false;
    int8_t   held_row_    = -1;    /* row button owning the current hold */
    int8_t   claimed_pot_ = -1;    /* the knob whose depth is being set  */
    bool     moved_       = false; /* a knob moved during this hold      */
    int8_t   nav_request_ = -1;    /* tapped row, handed to the next frame */
    int8_t   combo_held_  = -1;    /* combo in progress: held button ...   */
    int8_t   combo_tap_   = -1;    /* ... and the one pressed while held   */
    int8_t   combo_req_held_ = -1, combo_req_tap_ = -1;   /* for the next frame */
    uint32_t held_since_  = 0;
    float    press_phys_[kNumPots] = {};
    float    last_pos_    = -1.0f;
};
