# Blas

> *Blas*: coined by the 17th-century chymist Jan Baptist van Helmont for the invisible, active
> force or impulse that initiates motion and change within matter.

**A side-chain ducker that brings its own kick and snare, for the Hermetic Modular Alchemy Lab
(V2).** Stereo audio goes in, a trigger ducks it, and the same trigger plays a 909-style kick that
lands in the hole it just made. A second trigger does the same with a snare. The ducking can be
plain volume, low band only, or an EQ cut that follows each drum's tuning.

> **Status: v0.1.0, not yet run on hardware.** The firmware builds for the module, and the audio
> engine, the hold gesture, the CV depths and the web-editor descriptor pass the host tests in
> `tests/`. The sound of the drums and the feel of the gestures have not been heard or tried on a
> real Alchemy Lab. See [First flash checklist](#first-flash-checklist).

Blas is laid out like Athanor: three rows, a button per row, a CV input per row.

```
        LEFT               BUTTON   RIGHT              CV in
Row 1   [P1] DEPTH         B1       [P2] RELEASE       J5   duck
Row 2   [P3] KICK LEVEL    B2       [P4] KICK TUNE     J6   kick
Row 3   [P5] SNARE LEVEL   B3       [P6] SNARE TUNE    J7   snare

J1 / J2   audio in L / R            J3   kick trigger
J9 / J10  audio out L / R           J4   snare trigger
                                    J8   duck envelope out
```

Signal flow: **In → ducker → + kick + snare → soft clip → Out.** The drums are added after the
ducker, so they are never ducked themselves.

## Buttons

| Gesture | What it does |
|---|---|
| **Tap** a row's button | Opens that row's six-knob options page. Tap it again to go back. Tapping another row's button jumps straight to that row's page. |
| **Hold** a row's button **and turn a knob** | Sets how much that row's CV jack moves that knob. |
| **Hold B2 + B3** for two seconds | Opens the Settings menu (B1 steps through its pages, B2 or B3 leaves). |

Each button wears its row's colour: violet for the ducker, orange for the kick, blue for the snare.
The open page's button is bright. B1 dims as the signal ducks; B2 and B3 flash when their drum
plays. On the Depth ring a white pip jumps out to the Depth setting on every hit and slides home as
the signal recovers.

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
| P5 Focus | | Split: the crossover, 50 Hz to 1 kHz (224 Hz at noon). Carve: slides the cuts up to an octave either side of the drums' tuning (noon = on the note). |
| P6 Width | | Carve: how wide the cut is, narrow (left) to wide (right). |

**Kick** (tap B2)

| Knob | Range | |
|---|---|---|
| P1 Decay | 120 ms to 2 s | |
| P2 Bend | | Depth and length of the pitch drop. Zero is a pure thud. |
| P3 Punch | | A very fast pitch spike plus a level bump into the saturator: the knock at the front. |
| P4 Click | | Level of the click transient. |
| P5 Drive | | Saturation. Peak level stays put; the body gets thicker and louder. |
| P6 Duck | 0 to 100% | How much a kick trigger ducks. Zero: the kick plays without ducking. |

**Snare** (tap B3)

| Knob | Range | |
|---|---|---|
| P1 Decay | 80 to 600 ms | |
| P2 Snappy | | Amount of snare noise. |
| P3 Tone | | Brightness of the snare noise. |
| P4 Bend | | Pitch drop on the shell. |
| P5 Drive | | Saturation. |
| P6 Duck | 0 to 100% | How much a snare trigger ducks. |

Each drum has its own duck envelope. They share Attack, Hold, Release and Curve, and each has its
own amount (the two Duck knobs). Out of the box the kick ducks fully and the snare at 40%.

**Ghost triggers.** A trigger ducks whether or not its drum is audible. Turn Kick Level to zero and
J3 becomes a plain side-chain trigger for a kick that lives elsewhere.

## Duck types

| Type | What ducks | Use it for |
|---|---|---|
| **Volume** | The whole signal. | The classic pump. |
| **Split** | Only the band below the Focus crossover. Everything above passes untouched. | Making room for the kick while hats, pads and vocals stay put. |
| **Carve** | A dynamic EQ cut around each drum: just above the kick's tuning for kick triggers, just above the snare's tuning for snare triggers. Depth sets how deep (up to 36 dB), Width how wide. | The most transparent option. The cuts follow the Tune knobs and their CV. |

Switching type crossfades over about 12 ms, so you can do it live or from CV.

## Triggers (J3, J4)

A rising edge above 1.5 V fires the drum and its duck envelope.

With **Trigger accent** on (the default), the trigger's voltage sets how hard the drum hits: 1.5 V
is a soft hit, 4.5 V and above is full level. Reading the height adds one audio block (0.5 ms).
Turn accent off in Settings and every hit is full level with no added delay.

## CV (J5, J6, J7)

**J5** can move any ducker knob, **J6** any kick knob, **J7** any snare knob: the two on the main
page and the six on the row's options page. Every knob has its own depth, set like an attenuverter:

- **Hold the row's button and turn the knob you want the CV to move.** The first knob you turn is
  the one you are setting. Noon = off, right = positive, left = inverted. While you hold, the ring
  shows the depth: white for positive, cyan for inverted.
- **Hold without turning** and after a moment the row's rings show their current depths.
- **Release.** The knob goes back to its normal job, but its value stays where it was until you turn
  the knob back to it, so setting a depth never makes the sound jump.
- On the main page a button reaches its own row's two knobs. On an options page its button reaches
  all six.
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

| Page | P1 | P2 | P3 | P4 | P5 |
|---|---|---|---|---|---|
| 1 Setup | LED brightness | Trigger accent (off / on) | Preset slot | Preset save / load | J8 envelope (dip / bump / full) |

Pages 2 to 5 hold the **CV depths**, one page per knob page (main, duck, kick, snare), with each
depth on the same knob as the control it belongs to. These are the same values the hold-and-turn
gesture sets, so use whichever is handier.

## Presets and power-on

Everything above (all 24 knobs, the CV depths and the Setup options) is saved in a preset.

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
`blas.bin` in this folder is a ready-built copy of v0.1.0.

`lib/` is ignored by git. To track the SDK and libDaisy as submodules, as Athanor does, remove
`lib/` from `.gitignore` and add them with `git submodule add`.
