# Blas

> *Blas*: coined by the 17th-century chymist Jan Baptist van Helmont for the invisible, active
> force or impulse that initiates motion and change within matter.

**A side-chain ducker that brings its own kick and snare, for the Hermetic Modular Alchemy Lab
(V2).** Stereo audio goes in, a trigger ducks it, and the same trigger plays a 909-style kick that
lands in the hole it just made. A second trigger plays a snare, which does not duck. The kick can be
a 909, an 808 or an FM kick, and the snare a 909, an 808 or a clap. A techno rumble can follow the
kick, swelling up between hits. The ducking can
be plain volume, a band you place yourself, or an EQ cut that follows the kick's tuning.

Blas is laid out in three rows, a button per row, a CV input per row.

```
        LEFT               BUTTON   RIGHT              CV in
Row 1   [P1] DEPTH         B1       [P2] RELEASE       J5   duck
Row 2   [P3] KICK LEVEL    B2       [P4] KICK TUNE     J6   kick
Row 3   [P5] SNARE LEVEL   B3       [P6] SNARE TUNE    J7   snare

J1 / J2   audio in L / R            J3   kick trigger
J9 / J10  audio out L / R           J4   snare trigger
                                    J8   duck envelope out
```

Signal flow: **In → ducker → + kick + snare + reverb + rumble → soft clip → Out.** The drums, reverb and rumble
are added after the ducker, so they are never ducked themselves.

## Buttons

| Gesture | What it does |
|---|---|
| **Tap** a row's button | Opens that row's options page. Tap it again to go back (the kick has two pages: tap B2 for Kick, again for Rumble, again to go back). Tapping another row's button jumps straight to that row's page. A press during which you turned a knob is never a tap. |
| **Hold** a row's button **and turn one of its knobs** | Sets how much that row's CV jack moves that knob. On the main page a button reaches its own pair; on its own options page it reaches all six. |
| **Hold B2, tap B3** | Next kick model: 909 → 808 → FM. Keep B2 held and tap again to step on. |
| **Hold B3, tap B2** | Next snare model: 909 → 808 → Clap. |
| **Hold B2, tap B1** | Rumble on/off. Off closes the rumble's input, so the tail rings out rather than cutting. |
| **Hold B2 + B3** for two seconds | Opens the Settings menu (B1 steps through its pages, B2 or B3 leaves). |

Each button wears its row's colour: violet for the ducker; for the kick, orange (909), amber (808)
or pink (FM); for the snare, blue (909), teal (808) or green (Clap). After a model change the row's
two main rings show which model is on for a second; after the rumble combo they show off (left) or
on (right) in deep red. On the Rumble page B2 turns deep red.
The open page's button is bright. B1 dims as the signal ducks; B2 and B3 flash when their drum
plays. On the Depth ring a white pip jumps out to the Depth setting on every hit and slides home as
the signal recovers.

A combo needs a quick tap of the second button, so holding B2 + B3 still opens Settings. Holding B1
and tapping B2 or B3, and holding B3 and tapping B1, do nothing yet.

Changing page never makes anything jump: a knob only takes over once you turn it through the stored
value. A small pip shows where the knob physically is until then.

## The pages

**Main**

| Knob | Range | |
|---|---|---|
| P1 Depth | 0 to 100% | How far the signal ducks. 50% is 12 dB; 100% is silence at the bottom. |
| P2 Release | 20 ms to 2 s | Recovery time. |
| P3 Kick Level | | |
| P4 Kick Tune | 30 to 120 Hz | The pitch the kick settles on. |
| P5 Snare Level | | |
| P6 Snare Tune | 110 to 330 Hz | The lower of the snare's two shell tones. |

**Duck** (tap B1)

| Knob | Range | |
|---|---|---|
| P1 Attack | 0.5 to 100 ms | Time to reach full ducking. |
| P2 Hold | 0 to 250 ms | Time held at the bottom before recovering. |
| P3 Curve | | Shape of the recovery. Left: stays down, then lets go late. Noon: straight line. Right: lets go early with a long tail, like a compressor. |
| P4 Type | Volume / Split / Carve | See [Duck types](#duck-types). |
| P5 Focus | | Split: the centre of the band, 30 Hz to 2 kHz (245 Hz at noon). Carve: slides the cut up to an octave either side of the kick's tuning (noon = on the note). |
| P6 Width | | Split and Carve: how wide the cut is, like an EQ's Q. Narrow (left) to wide (right). |

**Kick** (tap B2)

The six knobs work on every model, each in its own way. A model change takes effect on the next
hit, so a ringing kick never changes voice halfway through.

| Model | Character | What changes |
|---|---|---|
| **909** | A punchy sine with a pitch sweep and a bright beater click. | As described below. |
| **808** | A rounder boom. | The pitch drops a third as far, the tail lasts half as long again, Click is a soft low thump instead of a bright one, and Drive is gentler. |
| **FM** | Two-operator FM: a metallic, techy attack over a sine body. | Click sets how bright the attack is (always a little bright, even at zero), Punch the modulator's ratio: 1 is warm, 2 hollow, in between clangs. |

| Knob | Range | |
|---|---|---|
| P1 Bend | | How far the pitch drops. Zero is a pure thud. |
| P2 Sweep | | How the pitch comes back down. Left: a fast snap, on the note within a few milliseconds, like an 808. Right: hangs high, then glides down over a few hundred milliseconds, like a 909. Does nothing with Bend at zero. |
| P3 Punch | | Makes the front hit harder than the tail. The first 30 ms stay at full level while the rest of the body drops, by up to 9 dB, and a short pitch knock is added to the start. Turn Level up to bring the tail back. |
| P4 Click | | The beater: a few milliseconds of bright noise and a 2.1 kHz ping on top of the hit. |
| P5 Drive | | Saturation. Peak level stays put; the body gets thicker and louder. |
| P6 Decay | 120 ms to 2 s | |

**Snare** (tap B3)

| Model | Character | What changes |
|---|---|---|
| **909** | Two shell tones with a pitch drop, plus snares. | As described below. |
| **808** | A ringing, tonal body with brighter, shorter snares. | Bend only drops the pitch a little. |
| **Clap** | Three quick noise bursts and a tail. | Tune places the clap's band (550 Hz to 1.65 kHz), Tone opens it up, Bend spreads the bursts (6 to 16 ms apart), Snappy sets the tail against the bursts and Decay its length. |

| Knob | Range | |
|---|---|---|
| P1 Decay | 80 to 600 ms | |
| P2 Snappy | | Amount of snare noise. |
| P3 Tone | | Brightness of the snare noise. |
| P4 Bend | | Pitch drop on the shell. |
| P5 Drive | | Saturation. |
| P6 Reverb | | A stereo room on the snare. One knob raises the level and lengthens the decay together, from a small tight room (0.3 s) to a hall (3.5 s). Zero is off. Settings can send the kick to it as well. |

Only the kick ducks, by the Depth amount. The snare
plays without ducking; to give it some, set `snare_amt` in `blas_dsp.h`.

**Rumble** (tap B2 twice)

The kick fed through a dark reverb, then drive, then a low-pass, and pumped by the kick's own duck
envelope: silent under each kick, swelling up in the gap before the next. The pump uses the Duck
page's Attack, Hold, Release and Curve at full depth, whatever Depth is set to, so it always
breathes with the kick. Mono, mixed in after the ducker like the drums.

| Knob | Range | |
|---|---|---|
| P1 Rumble | 0 to 100% | Level. Zero (the default) is off. At full it peaks a little below the kick. |
| P2 Tone | 50 to 400 Hz | Low-pass on the rumble: a deep sub at the left, a boomier low-mid at the right. |
| P3 Decay | 0.4 to 4 s | How long the rumble's room rings. Short is a tight, gated thump; long blurs into a drone. |
| P4 Drive | | Grit. Zero is clean; turning it up adds harmonics without much change in level. |
| P5, P6 | | Not used yet. |

The rumble comes from the kick's sound, so with Kick Level at zero there is no rumble. J6 can
modulate all four knobs (hold B2 and turn, as on the Kick page).

**Ghost triggers.** A kick trigger ducks whether or not the kick is audible. Turn Kick Level to zero and
J3 becomes a plain side-chain trigger for a kick that lives elsewhere.

## Duck types

| Type | What ducks | Use it for |
|---|---|---|
| **Volume** | The whole signal. | The classic pump. |
| **Split** | A band you place yourself: Focus sets the centre, Width how wide, Depth how deep (100% removes the centre). Everything outside the band passes. | Clearing a fixed range, e.g. the bass's low end, whatever the kick is tuned to. |
| **Carve** | A dynamic EQ cut just above the kick's tuning. Depth sets how deep (up to 36 dB), Width how wide. | The most transparent option. The cut follows Kick Tune and its CV. |

Switching type crossfades over about 12 ms, so you can do it live or from CV.

## Triggers (J3, J4)

A rising edge above 1.5 V fires the drum. A kick trigger also fires the duck envelope.

With **Trigger accent** on (the default), the trigger's voltage sets how hard the drum hits: 1.5 V
is a soft hit, 4.5 V and above is full level. Reading the height adds one audio block (0.5 ms).
Turn accent off in Settings and every hit is full level with no added delay.

## CV (J5, J6, J7)

**J5** can move any ducker knob, **J6** any kick knob, **J7** any snare knob: the two on the main
page and the six on the row's options page. Every knob has its own depth, set like an attenuverter:

- **Hold the row's button and turn the knob you want the CV to move.** B1 for duck knobs (J5), B2
  for kick knobs (J6), B3 for snare knobs (J7). The first knob you turn is the one you are setting.
  Noon = off, right = positive, left = inverted. While you hold, the ring shows the depth: white
  for positive, cyan for inverted.
- **On the main page** a button reaches its own row's two knobs. **On a row's options page** that
  row's button reaches all six: on the Kick page, hold B2 and turn any knob to set how much J6
  moves it. The other two buttons set nothing there.
- **Hold without turning** and after a moment the row's rings show their current depths.
- **Release.** The knob goes back to its normal job, but its value stays where it was until you turn
  the knob back to it, so setting a depth never makes the sound jump.
- **Depth range:** at full depth, ±5 V moves the knob half its travel. The rings follow the
  modulated position.
- Unpatched jacks contribute nothing, and depths default to off.

The CV is read in the audio callback, so a pitch CV that steps together with its trigger is already
in place for that hit.

## Envelope out (J8)

J8 puts out the duck envelope, to pump other modules. Three modes, chosen in Settings:

| Mode | At rest | On a hit |
|---|---|---|
| **Dip** (default) | 5 V | Falls by the Depth amount. Patch to a VCA's CV input and that VCA ducks with Blas. |
| **Bump** | 0 V | Rises by the Depth amount. |
| **Full** | 0 V | Rises to 5 V whatever Depth is set to. Use it with Depth at zero to duck only external gear. |

## Settings menu

Hold **B2 + B3** for two seconds. **B1** steps through the pages; **B2** or **B3** leaves.

| Page | P1 | P2 | P3 | P4 | P5 | P6 |
|---|---|---|---|---|---|---|
| 1 Setup | LED brightness | Trigger accent (off / on) | Preset slot | Preset save / load | J8 envelope (dip / bump / full) | Reverb on (snare / kick + snare) |

Pages 2 to 6 hold the **CV depths**, one page per knob page (main, duck, kick, snare, rumble), with each
depth on the same knob as the control it belongs to. These are the same values the hold-and-turn
gesture sets, so use whichever is handier. A depth starts at noon (off), and like every knob in the
menu it only starts moving once you turn the knob through noon.

## Presets and power-on

Everything above (all 28 knobs, the CV depths, the kick and snare models, rumble on/off and the Setup
options) is saved in a preset. Presets saved before the rumble was added are not carried over: the
first power-on starts from the defaults.

- Blas **remembers its state by itself**: three seconds after the last change it writes the current
  state to the first preset slot, which is the one loaded at power-on.
- Save to and load from the other slots in the Settings menu. A loaded preset becomes the current
  state.
- The preset area is shared between Alchemy firmwares. If the first slot holds another firmware's
  preset, Blas leaves it alone and does not autosave; saving a Blas preset to that slot from the
  Settings menu takes it over.

Plug in USB and the Hermetic Modular web programmer shows every knob, depth and setting by name,
with the jacks labelled.

## USB MIDI

Blas is a USB MIDI device on the same cable. Notes on any channel fire the drums, with velocity:

| Note | Drum |
|---|---|
| 35, 36 (B0, C1) | Kick |
| 38, 40 (D1, E1) | Snare |

## Building and flashing

Requirements: `git`, `make`, `arm-none-eabi-gcc` 12 or newer, `dfu-util` (and `g++` for the tests).

```sh
./setup.sh           # fetch the pinned Alchemy SDK (v0.13.0) + libDaisy into lib/, build libDaisy
make                 # build/blas.bin
make program-dfu     # flash; put the module in update mode first (press B3 in the boot window)
make program-live    # or: reboot the running module over USB and flash it
make test            # host-side checks, no hardware needed
```

If Athanor's `lib/` is already set up next door, `ln -s ../athanor/lib lib` saves the download.
`blas.bin` in this folder is a ready-built copy of the current source.

`lib/` is ignored by git. To track the SDK and libDaisy as submodules, as Athanor does, remove
`lib/` from `.gitignore` and add them with `git submodule add`.


> **This repo has been made with the help of an LLM**