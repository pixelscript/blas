/**
 * @file hold_pager.h
 * @brief A Pager that can stop listening to the pots for the length of a
 *        button hold. Header-only so the host tests can exercise it.
 */
#pragma once

#include <cstdint>
#include "alchemy/hw/alchemy_lab_layout.h"   /* kNumPots */
#include "alchemy/surface/pager.h"

/* ═════════════════════════════════════════════════════════════════════════
 *  Pager that can stop listening to the pots
 *
 *  While a row button is held, pot motion sets a CV depth instead of the
 *  knob's own parameter. Feeding the Pager a frozen copy of the pots for
 *  the length of the hold keeps every parameter exactly where it was; on
 *  release catch is re-armed, so the moved knob has to be turned back to
 *  its value before it takes over again (the ring shows a pip where the
 *  knob physically is). Nothing jumps.
 * ═════════════════════════════════════════════════════════════════════════ */
class HoldPager : public alchemy::Pager
{
  public:
    HoldPager(uint8_t num_pages, uint8_t num_pots) : Pager(num_pages, num_pots) {}

    void Freeze(const float* phys)
    {
        for (uint8_t i = 0; i < alchemy::kNumPots; i++) frozen_[i] = phys[i];
        frozen_on_ = true;
    }
    void Thaw()
    {
        if (!frozen_on_) return;
        frozen_on_ = false;
        relock_    = true;
    }
    bool Frozen() const { return frozen_on_; }

    void Update(const float* phys, uint32_t t_ms) override
    {
        if (relock_)
        {
            relock_ = false;
            LockPage(Page(), phys);
        }
        Pager::Update(frozen_on_ ? frozen_ : phys, t_ms);
    }

  private:
    float frozen_[alchemy::kNumPots] = {};
    bool  frozen_on_        = false;
    bool  relock_           = false;
};

