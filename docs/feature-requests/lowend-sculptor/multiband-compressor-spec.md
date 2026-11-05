# Feature request: LowEnd Sculptor — 3-band bass multiband compressor (Rack FX)

| Field | Value |
| :--- | :--- |
| Status | Draft for review. Not implemented. |
| Type | Feature, new audio effect, new UI panel |
| Target branch | `feature/multiband_compressor` |
| Placement | Rack FX panel, last tile (index 5), after 3D |
| Reference design | `rackfx.jpeg` (panel and tile) and `tonegrid-multiband-compressor.jpeg` (editor page) |
| Verified against | `main` at `d1c6c58`, JUCE 8.0.6, application version 1.1.44 |
| Supersedes | The earlier pedal-based draft of this document (see Appendix A) |

Requirement words: **MUST** is a release condition, **SHOULD** is expected unless a
review records a reason, **MAY** is optional.

---

## 1. Summary

Add a three-band bass multiband compressor as a **Rack FX item**: a stereo processor that
splits the signal at two user crossovers, compresses each band independently, optionally
saturates the high band, and sums the bands with an exactly flat magnitude response. The
item appears as the last tile of the Rack FX panel, beside MOD, DELAY, REVERB, EQ and 3D, with a
status LED and a gear that opens a full editor page containing a live spectrum
analyzer, per-band controls and a gain-reduction meter.

Why a Rack FX item and not a pedal:

1. The bass stage belongs after the amp and cabinet, where the low end actually exists, and
   before the stereo widener, so the low band stays centered.
2. It must not consume one of the thirteen pedal slots, must not change the pedal order
   oracle, and must not require new pedal artwork or a rack re-layout.
3. The reference design needs five knobs per band, two crossover sliders, an analyzer and a
   meter. A pedal card supports exactly three rotary controls and has no drawing surface.
4. Rack FX parameters persist inside the existing `<OutputDSP>` element, so rigs that do not
   use the module stay byte-identical (section 9).

---

## 2. Platform constraints (verified in this tree)

These are facts about the codebase, not preferences. The design below assumes all of them.

| Constraint | Evidence | Consequence for this feature |
| :--- | :--- | :--- |
| The product is a standalone JUCE **application**, not a plugin | `juce_add_gui_app` in `CMakeLists.txt`; no `juce_add_plugin`, no `PluginProcessor` | No VST3/AU/AUv3 export, no plugin wrapper, no `getStateInformation` |
| No `AudioProcessorValueTreeState` | absent from `Source/` | Parameters are plain structs published through `ParameterMailbox<T>` |
| Engine rate is fixed at 48 kHz | `DeviceRateProcessor::engineRate`; device rates 8 kHz to 192 kHz are converted at the graph boundary | The DSP takes `sampleRate` as an argument and is rate-agnostic, but ships running at 48000 |
| Rack FX are `juce::AudioProcessor` nodes in an `AudioProcessorGraph` | `Source/OutputDspNodes.h`, `Source/Core/ProcessingGraph.cpp` | The effect is a `NodeBase` subclass with a `ParameterMailbox` and no UI types |
| Control-to-audio transport is lock-free and trivially copyable | `Source/ParameterMailbox.h` (`static_assert(std::is_trivially_copyable_v<T>)`) | The parameter struct stays a plain aggregate of floats and bools |
| Popup pages derive from `popup::View` | `Source/Shared/UI/PopupStyle.h`, `Source/EffectsControls.h` | The editor is a `popup::View` with `popupMinimumWidth()` and `popupContentHeight()` |
| There is no third-party test framework | `Tests/OfflineTests.cpp` dispatch plus `add_test` in `CMakeLists.txt` | Tests are header functions that append PASS/FAIL lines and return `bool` |
| JUCE DSP is already linked where effects live | `target_link_libraries(tonegrid_effects PRIVATE juce::juce_dsp)` | The analyzer may use `juce::dsp::FFT` |
| License is AGPL-3.0-only, Windows CI is publication-gated | `README.md`, `PUBLICATION-STATUS.json` | No new third-party code, no vendored blobs, no signed-build claims |

---

## 3. Scope

### 3.1 In scope

1. One DSP engine with no UI dependency, in the house style of the other rack effects.
2. One graph node, inserted between Amp EQ and 3D SOUND.
3. Persistence in `<OutputDSP version="8">` with full backward compatibility.
4. One RACK FX panel tile and one popup editor page matching the reference designs, including
   the spectrum analyzer and the gain-reduction meter.
5. One offline test suite covering the crossover, the dynamics, the saturation, the dry/wet
   path, persistence, and the CPU ceiling.

### 3.2 Out of scope

- Lookahead, oversampling, external sidechain input, mid/side processing.
- Per-band solo, per-band bypass, per-band mute.
- A limiter or output ceiling inside the module. Level management remains `MASTER OUTPUT`
  plus the existing output meter.
- MIDI control of individual bands. The existing `PedalMidiLearnView` covers pedals only.
- Per-preset values for this module. Section 9.3 explains why v1 ships it bypassed.
- Any change to the thirteen pedals, the pedal order oracle, `ToneBank`, or pedal artwork.

---

## 4. Signal chain placement

Current guitar path (`Source/Core/ProcessingGraph.cpp`, `buildGuitarGraph`):

```
audio input -> board::Processor (13 pedals) -> amp -> cabinet
            -> Normalization -> AmpEq -> Spread -> Mod -> Delay -> Reverb -> output
```

New path:

```
audio input -> board::Processor (13 pedals) -> amp -> cabinet
            -> Normalization -> AmpEq -> LowEnd Sculptor -> Spread -> Mod -> Delay -> Reverb
```

Rationale, in review order:

1. **After the cabinet and Amp EQ.** The module shapes the tone the player hears. Placing it
   before the cabinet would make the low band react to frequencies the cabinet removes.
2. **Before 3D SOUND (Spread).** The widener creates a stereo image. A low-band compressor
   placed after it would either be stereo and cost twice the CPU for identical channels, or
   mono and collapse the image. Before it, the low end is summed once and stays centered,
   which is also the correct musical result for bass.
3. **Before MOD, DELAY and REVERB.** Time-based effects must not feed a detector; tails would
   pump the compressor.
4. **After Normalization.** Normalization is a per-capture static trim, so position does not
   change its behaviour; keeping it in place minimizes the diff.

Tile order is presentational, not the audio order. The reference panel groups modulation, then
time, then tone. The audio order is still section 4 above, and the editor page MUST state its
own position in the chain in the help line, for example: "Acts after Amp EQ and before 3D
Sound." PITCH sets the precedent for a control whose UI position differs from its audio
position; the difference there is documented in its help text, and this feature must do the
same.

Latency: **zero samples**. The module adds no delay and MUST NOT change
`IRenderProcessor::latencySamples()`. A latency change would move recorded and looped audio
against the click.

---

## 5. User interface

### 5.1 Entry point: the Rack FX panel

`rackfx.jpeg` defines the entry point: a dedicated **RACK FX** panel, a dark surface with a
left vertical rule, the heading `RACK FX`, and one row of square icon tiles. The module is the
last tile.

Tile order, left to right, exactly as drawn:

`MOD` · `DELAY` · `REVERB` · `EQ` · `3D` · `SCULPTOR`

`SCULPTOR` is index 5, the final tile.

#### 5.1.1 Tile anatomy

Proportions are normative; they scale with the panel width.

1. Square face, width to height ratio about 1 : 1.05, corner radius about 4 percent of the
   width, face fill one step darker than the panel surface, 1 pixel outline at low alpha.
2. Status LED: a filled circle, diameter about 5 percent of the tile width, centred
   horizontally, about 10 percent of the tile height below the top edge.
3. Icon: a line drawing (not a bitmap), centred in the square, spanning about 62 percent of
   the tile width and about 40 percent of its height.
4. Caption: below the square, uppercase, bold, about 9 percent of the tile width, with
   letter spacing. `SCULPTOR` is the longest caption and is the fit reference.

#### 5.1.2 States

| State | LED | Icon | Outline |
| :--- | :--- | :--- | :--- |
| On | Cyan, with a soft glow | Full brightness | Normal |
| Off | Dark grey, no glow | About 35 percent alpha | Normal |
| Hover | Unchanged | Unchanged | Brightened |
| Keyboard focus | Unchanged | Unchanged | 2 pixel ring inside the outline, in the selected colour |
| Disabled (audio not ready) | Dark grey | 20 percent alpha | Dimmed |

The tile is never hidden when the effect is off. A tile whose audio is not ready is still
clickable, so the editor opens while the LED is dark.

#### 5.1.3 Icons

Every icon is a `juce::Path` drawn in code, in the style of `paintGear` in
`Source/AmpChassis.h`. No binary assets, no provenance entries.

| Tile | Icon | Colour |
| :--- | :--- | :--- |
| MOD | Three stacked horizontal waves | Purple |
| DELAY | Single outward spiral | White |
| REVERB | Mountain outline, flat base | White |
| EQ | Single waveform pulse | Cyan |
| 3D | Tight spiral | Grey |
| SCULPTOR | Three overlapping band curves: low rising, mid falling, high rising | Cyan |

The `SCULPTOR` icon mirrors the analyzer: three curves that cross at two points, which is the
crossover behaviour the effect actually produces.

#### 5.1.4 Interaction

- Click, `space` or `enter` toggles the effect. The LED follows the module state and is the
  only state indicator.
- The editor page opens from a small gear glyph in the tile's bottom-right corner. It appears
  on hover and whenever the tile holds keyboard focus, and it is the only child control inside
  the tile. This keeps the reference design free of permanently visible buttons while keeping
  the page discoverable.
- Right-click opens a menu with `Open` and `Reset`. `Reset` restores the module defaults and
  leaves the on/off state unchanged, the same rule as the page's own `Reset`.
- One page instance per host, raised to the front on a second open. Same ownership pattern as
  `ampEqDialog`, `spreadControls`, `reverbControls` and the effect dialogs.
- The tile is gated by the shared `ready()` gate of `RigHost`, like the other rack items.

#### 5.1.5 Panel layout

- Tile pitch: at least 150 pixels per tile including the gap, so six tiles fit a 1360 pixel
  window without horizontal scrolling. At 1660 pixels the pitch is about 250 pixels.
- The heading is left-aligned with the first tile, uppercase, bold, about 1.9 percent of the
  window width. The vertical rule sits at the panel's left edge, outside the heading.
- The dark surface is intentional: the panel reads as a rack unit against the lighter chassis.
  It MUST stay dark in any theme, and the tile outlines and captions MUST keep a contrast ratio
  of at least 4.5 : 1 against it.
- The panel scales with width only. Height is derived, so no tile is ever clipped.

#### 5.1.6 Relationship to the current build

The current build has no RACK FX panel. It draws four post-FX items inside the amp chassis
(`ampui::paintChassis` in `Source/AmpChassis.h`, positioned by `postFxStart` / `postFxStep` in
`Source/CompactAmpLayout.h`), each with a toggle and a gear: 3D SOUND, MOD, DELAY, REVERB.
AMP EQ has its own gear near the gain knob and PITCH sits elsewhere.

The reference panel therefore differs in three ways, and each needs a decision:

| Reference panel | Current build | Recommended handling |
| :--- | :--- | :--- |
| `EQ` tile | AMP EQ gear near the gain knob | The tile opens the existing Amp EQ page. |
| No `PITCH` tile | PITCH control exists | Leave PITCH where it is; it is not part of the panel. |
| No toggle or gear buttons | Toggle and gear per item | Tile click toggles; gear on hover opens the page (5.1.4). |

Recommendation: build the panel in this milestone, because the reference design is what the
feature is judged against, but keep behaviour identical for the five existing items. The panel
is then a presentation change only, with no audio path or parameter change for anything except
the new module.

Captions also shorten to fit the tile: the current `3D SOUND` becomes `3D`. Tooltips carry the
full names, so nothing is lost.

If the panel is deferred to a separate branch, the fallback is the current four-item strip with
a fifth item and `postFxStep` reduced from 120 to 96 face units. That fallback MUST be recorded
in the issue before P3 starts, because the two options produce different screenshots and
different test expectations.

#### 5.1.7 Default state

The module is **off** in a fresh rig and after every preset load. The tile is present, dark, and
clickable; the page can be configured before the module is switched on.

### 5.2 Editor page layout

Overall structure follows the reference image. Proportions are normative; pixel values are
targets for the 1080-wide case.

| Region | Height share | Contents |
| :--- | :--- | :--- |
| Header | ~7% | TONEGRID mark, title `LOWEND SCULPTOR`, `ON` switch, `Reset` button |
| Analyzer | ~44% | spectrum, band curves, band tints, `Fc1`/`Fc2` markers and labels, `LOW`/`MID`/`HIGH` captions |
| Band bar | ~9% | band tabs `LOW BAND` / `MID BAND` / `HIGH BAND`, `Crossover 1 (Fc1)` slider + field, `Crossover 2 (Fc2)` slider + field |
| Control row | ~28% | five band knobs, vertical rule, right column |
| Meter row | ~12% | gain-reduction bar, numeric readout |

Panel sizing:

- `popupMinimumWidth()` returns **1080**. Below that the page must not open narrower; the
  popup host already clamps to this value.
- `popupContentHeight(width)` returns `std::lround(width * 0.575)`, clamped to a minimum of
  560, so the page keeps the reference aspect ratio across window sizes.
- No content may be clipped at 1080 wide. The analyzer scales with width; knob diameters
  clamp between 48 and 96 pixels.
- The header `ON` switch and `Reset` button are in-page controls, not popup-window controls.
  The popup title bar remains the only close affordance.

### 5.3 Analyzer

Purpose: show what the player is feeding in, and what each band is currently doing to it.
It is a guide, not a measurement instrument; the meter numbers are normative, the drawing is
not.

- Input tap: the module input, before the crossover. The spectrum is therefore stable while
  the compressors work, and the curves show the difference.
- FFT: 2048-point Hann window, `juce::dsp::FFT` order 11, computed on the message thread from
  a copied snapshot. Never on the audio thread.
- Frequency axis: logarithmic, 20 Hz to 16 kHz, with grid labels at 20, 50, 100, 200, 500,
  1k, 2k, 5k, 10k, 16k. (The reference image stops at 10 kHz. 16 kHz is specified because
  guitar harmonic content continues above it and the extra decade costs no space on a log
  axis. Deviating from the image is permitted here; the labels above are not optional.)
- Amplitude axis: `-12 … +12 dB` for the band curves, labelled `+12 / 0 / -12` on the left.
  The spectrum uses the same box with an internal `-90 … 0 dB` scale.
- Grid: 1 octave vertical lines, 6 dB horizontal lines, drawn in the outline colour at low
  alpha. Band tints: LOW red, MID steel blue, HIGH amber, at 18 to 28 percent alpha.
- Spectrum drawing: filled path with a 1-pixel top stroke. Ballistics: rise to a new peak
  within 33 ms, fall at 12 dB per second. This produces the readable, slightly smoothed
  shape in the reference image.
- Band curve overlay: for each band, 128 log-spaced points at
  `20*log10(|H_band(f)|) + makeupDb - gainReductionDb(band)`, where `H_band` is the band's
  crossover response including its allpass term. Fill between the curve and the 0 dB line,
  in the band colour, only where the curve is below 0 dB.
- Crossover markers: vertical dashed lines at `Fc1` and `Fc2`, at their true log positions,
  with the caption `Fc1` / `Fc2` above and the frequency value below. Values update live
  while a crossover slider moves.
- Refresh: 30 frames per second through a `juce::Timer`. Repaint may be limited to the
  analyzer bounds. No allocation per frame after the first.

### 5.4 Band bar and band editing model

- The five knobs edit **one band at a time**, selected by the three tabs. The tab labels are
  `LOW BAND`, `MID BAND`, `HIGH BAND`; the active tab is underlined in the selected colour.
- The active band's name appears centered above the knob row, as in the reference image.
- Switching tabs MUST update the five sliders without emitting change events, and MUST NOT
  alter any value. A tab switch is a view change only.
- Each tab MAY show a small gain-reduction dot beside its label. If shown, it uses the same
  value as the meter for that band.

### 5.5 Right column

Separated from the knobs by a 1-pixel vertical rule, top to bottom:

1. `HIGH SATURATION` label, `DRIVE` knob, range `0.0 … 10.0`, one decimal.
2. `LOW SC HPF` label, toggle switch, off by default. The help text explains that it
   high-passes the low band's detector, not the audio.
3. `DRY/WET` knob, `0 … 100 %`, one decimal, cyan arc while below 100.
4. `MASTER OUTPUT` knob, `-24 … +24 dB`, one decimal, with an explicit sign for positive
   values.

### 5.6 Gain-reduction meter

- Horizontal bar, scale `-24 dB … 0 dB`, left to right.
- Value: the largest reduction across the three bands, in dB, positive number.
- Colour ramp along the scale: cyan, green, yellow, orange, red. The filled extent reaches
  the current value; the unfilled remainder is drawn at low alpha.
- Peak hold: a 2-pixel marker that holds the maximum for 1.5 s, then decays at 20 dB per
  second.
- Numeric readout to the right of the bar, one decimal, for example `-3.5 dB`. Shows `0.0 dB`
  when the module is off or when no band is compressing.

### 5.7 Interaction and state rules

- `ON` toggles bypass with an 8 ms equal-power crossfade inside the node, matching the
  existing pedal bypass ramp. No click at any input level.
- `Reset` restores every parameter of the module to its default (section 7) and leaves the
  ON state and the selected band unchanged. If the module is on, resetting must not click:
  apply the same 8 ms ramp to the gain change.
- Double-click on any rotary control returns that control to its default.
- A crossover value is committed only when `Fc1 < Fc2`. The ranges are disjoint (section 7),
  so this holds by construction; the UI must still clamp rather than swap.
- Every control MUST have a tooltip and a help string in the style of
  `tonegrid::effectHelp` (`Source/EffectsControls.h`), shown in a help line at the bottom of
  the page as the other effect panels do.
- Keyboard: standard JUCE focus order, arrow keys adjust the focused control, escape closes
  the popup.
- The page MUST tolerate a null or not-ready audio engine: it opens, shows defaults, and
  does not assert. This is required by the existing UI snapshot tests.

### 5.8 Test hooks

Every interactive component carries a stable component ID, used by automation:

`multiband-on`, `multiband-reset`, `multiband-band-low`, `multiband-band-mid`,
`multiband-band-high`, `multiband-cross1`, `multiband-cross2`, `multiband-threshold`,
`multiband-ratio`, `multiband-attack`, `multiband-release`, `multiband-makeup`,
`multiband-drive`, `multiband-sc-hpf`, `multiband-mix`, `multiband-output`,
`multiband-meter`, `multiband-analyzer`.

Each also gets a human-readable `setTitle` such as `Low band threshold`. The existing
screenshot tests take snapshots by walking children and IDs; this page must be reachable the
same way.

---

## 6. Naming

| Surface | Value |
| :--- | :--- |
| Display name | `LOWEND SCULPTOR` (one word, as in the editor reference) |
| Tile caption | `SCULPTOR`; tooltip `LowEnd Sculptor multiband compressor` |
| Panel heading | `RACK FX` |
| Tile component ID | `rackfx-sculptor` |
| Node name | `ToneGrid LowEnd Sculptor` |
| Popup title | `LowEnd Sculptor` |
| Namespace | `tonegrid::multiband` |
| Internal id | `multiband` |
| Rig element | `<OutputDSP version="8">` with child `<Multiband version="1">` |

---

## 7. Parameter reference

Twenty-two persisted values: twenty continuous and two switches. All values are plain floats
and bools so the struct stays trivially copyable.

### 7.1 Per-band dynamics (five per band)

| Parameter | ID | Range | Default LOW | Default MID | Default HIGH | Unit | Skew |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Threshold | `threshold` | -48 … 0 | -18.0 | -14.0 | -12.0 | dB | linear |
| Ratio | `ratio` | 1 … 20 | 4.0 | 3.0 | 2.5 | :1 | linear |
| Attack | `attack` | 0.5 … 200 | 40.0 | 20.0 | 10.0 | ms | logarithmic |
| Release | `release` | 10 … 2000 | 180.0 | 100.0 | 80.0 | ms | logarithmic |
| Makeup | `makeup` | -12 … +24 | 0.0 | 0.0 | 0.0 | dB | linear |

### 7.2 Crossovers

| Parameter | ID | Range | Default | Unit | Skew |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Crossover 1 | `cross1` | 40 … 400 | 160 | Hz | logarithmic |
| Crossover 2 | `cross2` | 500 … 8000 | 1800 | Hz | logarithmic |

The ranges are deliberately disjoint, so `Fc1 < Fc2` is guaranteed by validation rather than
by clamping in the DSP. This also keeps the smallest legal ratio at 500/400 = 1.25, where the
crossover construction below remains valid to numerical precision.

### 7.3 Global

| Parameter | ID | Range | Default | Unit | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| High saturation drive | `drive` | 0 … 10 | 0.0 | — | 0 = hard bypass of the saturator |
| Low sidechain HPF | `scHpf` | off / on | off | — | detector only, not the audio path |
| Dry / wet | `mix` | 0 … 100 | 100.0 | % | 100 = fully processed |
| Master output | `output` | -24 … +24 | 0.0 | dB | last gain stage |
| Module enable | `enabled` | off / on | off | — | 8 ms crossfade bypass |

Default state therefore matches the values printed in the reference image: LOW `-18.0 dB`,
`4.0:1`, `40.0 ms`, `180.0 ms`, `0.0 dB`, crossovers `160 Hz` and `1800 Hz`, drive `0.0`,
mix `100.0 %`, output `+0.0 dB`.

### 7.4 Validation

`multiband::valid(const Parameters&)` MUST reject:

1. Any non-finite value.
2. Any value outside its range in the tables above.
3. `cross1 >= cross2`.

Out-of-range values are never clamped silently during load; the element is rejected and the
rig fails to load, matching `ampEq::valid`, `reverb::valid` and `delay::valid`.

---

## 8. DSP requirements

### 8.1 Crossover: phase-compensated Linkwitz-Riley 4

Definitions. `B_LP2(f)` and `B_HP2(f)` are second-order Butterworth sections with
`Q = 1/sqrt(2)`; `LP4 = B_LP2^2`, `HP4 = B_HP2^2`; `AP2(f)` is a second-order allpass with
`Q = 1/sqrt(2)`.

| Band | Signal path |
| :--- | :--- |
| LOW | `LP4(Fc1) * AP2(Fc2)` |
| MID | `HP4(Fc1) * LP4(Fc2)` |
| HIGH | `HP4(Fc1) * HP4(Fc2)` |

Sum, exactly:

```
LOW + MID + HIGH = LP4(Fc1)*AP2(Fc2) + HP4(Fc1)*[ LP4(Fc2) + HP4(Fc2) ]
                 = AP2(Fc1)*AP2(Fc2)
```

`LP4 + HP4 = AP2` because, in the analog prototype with `D = s^2 + sqrt(2)s + 1`,

```
1/D + s^2/D = (s^2 + 1)/D
```

which is a second-order allpass of unity magnitude. Bilinear transform preserves the identity,
so the digital sum is unity-magnitude at every frequency to float precision. Appendix B gives
the measured values and warns about a measurement mistake that produces false errors of tens
of dB.

Consequences that MUST hold:

1. With all ratios at 1:1, drive at 0, makeup at 0 and mix at 100 percent, the module is
   magnitude-flat within **0.1 dB** from 20 Hz to 20 kHz, at every legal crossover pair. This
   is the primary acceptance test.
2. Because the wet path is `AP2(Fc1)*AP2(Fc2)` and not the identity, the module's own dry
   path MUST also pass through `AP2(Fc1)*AP2(Fc2)`. A raw dry path would comb at intermediate
   dry/wet values with a notch at each crossover.
3. Coefficient updates: recompute on the audio thread when a crossover changes, in 16-sample
   chunks, in the house style already used by the Graphic EQ. No allocation, no locks.
4. State reset on crossover change: keep filter state; do not clear it. Clearing produces a
   click. A 20 ms logarithmic ramp of `Fc1` and `Fc2` towards the new value is the preferred
   way to keep the motion click-free; the acceptance test in 11.1 T7 measures this.

### 8.2 Per-band compressor

- Topology: feedforward, peak detector, one gain per band, applied after the band filters.
- Detector input: the band signal after the crossover and before the band's gain stage.
- Detector filter: 2nd-order Butterworth high-pass, 80 Hz, applied only in the detector of the
  LOW band when `LOW SC HPF` is on. The audio path is never filtered by this control.
- Stereo: one detector per band driven by the larger of the two channels, one gain applied to
  both channels. This keeps the stereo image stable when the amp produces different channels
  and costs nothing on the usual dual-mono signal.
- Static curve, in dB, with threshold `T`, ratio `R` and soft knee width `W = 6 dB`:
  - `x <= T - W/2`: gain = 0
  - `|x - T| < W/2`: gain = `(1/R - 1) * (x - T + W/2)^2 / (2W)`
  - `x >= T + W/2`: gain = `(1/R - 1) * (x - T)`
- Ballistics: one-pole smoothing in the dB domain,
  `a = exp(-1 / (0.001 * timeMs * sampleRate))`, attack applied when the target gain is lower
  than the current gain, release otherwise. No program-dependent release in v1.
- Attack and release are per band and independent. A band at 1:1 MUST produce exactly 0 dB of
  reduction, byte for byte, so that the flat-sum test is not perturbed by the detector.
- Makeup is a static linear gain, applied after the band gain, before the sum.
- Gain smoothing: the applied gain is smoothed with a 5 ms one-pole to prevent zipper noise on
  parameter changes. A parameter step MUST NOT produce a sample-to-sample gain jump larger
  than 0.5 dB.

### 8.3 High-band saturation

- Applied to the HIGH band only, after its compressor and makeup, before the sum.
- `drive = 0` MUST hard-bypass the saturator: the high band path is bit-identical to the
  unsaturated path. This protects the flat-sum test and makes "off" mean off.
- Normative behaviour:
  1. Monotonic (odd) transfer, no DC offset, no aliasing introduced by the effect itself
     beyond what the 48 kHz sample rate implies.
  2. Unity small-signal gain within 0.1 dB at every drive value, so moving the knob changes
     character, not loudness.
  3. No output above 0 dBFS for any input up to +12 dBFS.
  4. Continuous: moving drive from 0 to 0.1 MUST NOT step the level by more than 0.1 dB.
- One acceptable construction, given only to remove ambiguity, is a wet/dry blend toward a
  soft clipper: `d = drive/10`, `y = (1-d)*x + d*tanh(2.5*x)/2.5` scaled to unity
  small-signal gain. The implementer may choose another shape that satisfies the four rules
  above.

### 8.4 Dry/wet and output

- `dry/wet` is a linear blend inside the module between the phase-aligned dry path (8.1
  requirement 2) and the summed bands.
- `MASTER OUTPUT` is a static gain, `-24 … +24 dB`, smoothed over 10 ms.
- The output is not limited. Peaks above 0 dBFS are the player's responsibility, as with every
  other rack effect, and the existing output meter shows them.

### 8.5 Bypass

- `enabled = false` crossfades to the phase-aligned dry path over 8 ms and keeps all internal
  state running, so re-enabling does not restart the detectors.
- The bypass crossfade is equal-power, matching the pedalboard implementation.
- When bypassed, reported gain reduction is 0 dB and the analyzer may keep showing the input
  spectrum.

### 8.6 Numerical rules

- `juce::ScopedNoDenormals` at the top of `processBlock`, as every other node does.
- No allocation, no locks, no file or network access on the audio thread.
- No NaN or infinity for any legal parameter combination and any input in
  `[-1, 1]`; the test suite sweeps the parameter space and asserts finiteness.
- Mono or stereo buffers: the node declares stereo, and MUST handle a one-channel buffer
  without reading channel 1.

---

## 9. Persistence

### 9.1 Format

`<OutputDSP version="8">` gains one child element:

```xml
<OutputDSP version="8" ampNormalizationEnabled="1" spreadEnabled="1" ...>
  <Reverb version="1" .../>
  <Pitch version="1" .../>
  <Mod version="1" .../>
  <Delay version="1" .../>
  <AmpEq version="1" .../>
  <Multiband version="1" enabled="0" cross1="160" cross2="1800"
             lowThresholdDb="-18" lowRatio="4" lowAttackMs="40" lowReleaseMs="180" lowMakeupDb="0"
             midThresholdDb="-14" midRatio="3" midAttackMs="20" midReleaseMs="100" midMakeupDb="0"
             highThresholdDb="-12" highRatio="2.5" highAttackMs="10" highReleaseMs="80" highMakeupDb="0"
             drive="0" scHpf="0" mixPercent="100" outputDb="0"/>
</OutputDSP>
```

- Version 8 is written only when the module differs from its default state, using a
  `requiresMultiband(s)` helper in `Source/OutputDspParameters.h` in the same style as
  `requiresExpanded` and `requiresAmpEq`. Rigs that never touch the module therefore keep
  today's version number and stay byte-identical, including the compatibility fixture at
  `Tests/Fixtures/compatibility.xml`.
- The version expression MUST test `requiresMultiband` first, so version 8 outranks 7, 6, 5
  and 4 for a rig that also uses advanced EQ or pitch/mod/delay.
- `<Multiband>` is written after `<AmpEq>`, and only at version 8.

### 9.2 Decode rules, and the defect this change exposes

`decode` in `Source/OutputDspState.h` currently decides what to expect from the **parent**
version at six separate places. Adding version 8 without rewriting all six silently
downgrades or rejects valid rigs. Each of these MUST be updated together:

| Site | Today | Required |
| :--- | :--- | :--- |
| Accepted versions | `"1" … "7"` | add `"8"` |
| Reverb type gate | Spring only for `3 … 7` | Spring also for `8` |
| Pitch/Mod/Delay node count | expects 1 for `4 … 7` | expects 1 for `4 … 8` |
| Pitch/Mod/Delay parse block | entered for `4 … 7` | entered for `4 … 8` |
| AmpEq node count | expects 1 for `5 … 7` | expects 1 for `5 … 8` |
| AmpEq parse block | entered for `5 … 7` | entered for `5 … 8` |
| AmpEq child version | derived from the parent: `parent==7 ? 3 : parent==6 ? 2 : 1` | read the child's own `version` attribute and accept 1, 2, 3 with their own required attributes |

Rules:

1. Version 8 requires exactly one `<Multiband>` child; versions below 8 require none. Any
   deviation fails the load.
2. `<Multiband>` is validated with `multiband::valid`; out-of-range values fail the load.
3. `enabled` and `scHpf` accept only `"0"` and `"1"`; every numeric attribute must parse
   completely with `strtod`, as the existing lambdas already enforce.
4. Version 8 rigs are rejected by earlier builds, because the accepted-version list is
   explicit. Downgrade is unsupported; this belongs in the release note.
5. The existing preference file also stores an `<OutputDSP>` element
   (`BoardMain.cpp`, the preference writer). It goes through the same encoder, so it inherits
   the same rules and needs no separate change.

Regression test (this is the one that catches a partial update of the table above): build a
settings object with **every** feature at a non-default value — advanced Amp EQ with depth,
Spring reverb, Pitch transpose, Mod on, Delay on, spread on, and the multiband on — encode,
decode, and compare every field. A decode that drops one subtree must fail the test, not pass
because the rig happened to be simple.

### 9.3 Presets and preferences

- `ToneBank` presets build `output::Settings` from JSON and do not carry `<OutputDSP>` XML, so
  **no ToneBank change is required**. Selecting a preset leaves the module at its defaults,
  that is, off. This matches how presets already reset reverb, delay and spread.
- Letting a preset set the module would require a new JSON block and a `schemaVersion` bump.
  That is a follow-up, not part of this feature. It is listed in section 14 so the decision is
  recorded rather than forgotten.

---

## 10. Performance

- DSP: one stereo instance at 48 kHz MUST process 10 seconds of audio in at most 200 ms,
  measured with the existing harness in `Source/BoardTests.h` that prints the CPU fraction.
  The test asserts a hard ceiling of 2 percent of real time; the expected value is well under
  1 percent.
- Analyzer: at most 4 ms of message-thread work per frame at 1080 x 620, 30 frames per second.
- Memory: fixed at preparation time. Scope ring 8192 samples, FFT buffers for 2048 points,
  three biquad cascades per band per channel. No growth with block size.
- The module MUST NOT add a node to the graph when it is disabled. It stays connected and
  bypasses internally, because reconnecting nodes at run time is a glitch source; the cost of
  the bypass path is a crossfade and a filter cascade at unity gain, which is acceptable.

---

## 11. Test plan

Tests are header functions appended to a report and dispatched by an argument, registered
with `add_test`. No third-party framework. Add `Tests/MultibandTests.h`, a
`--multiband-test` dispatch line in `Tests/OfflineTests.cpp`, and:

```cmake
add_test(NAME ToneGrid.Multiband COMMAND ToneGridOfflineTests --multiband-test)
```

### 11.1 DSP tests

| ID | Test | Pass condition |
| :--- | :--- | :--- |
| T1 | Flat sum, magnitude | Unit impulse through the full module, ratios 1:1, drive 0, makeup 0, mix 100. FFT of 65536 samples. Deviation from 0 dB is at most 0.1 dB from 20 Hz to 20 kHz, at the pairs (40, 500), (80, 800), (160, 1800), (350, 4000), (400, 8000). |
| T2 | Flat sum, rate independence | T1 repeated at 44100 and 96000 sample rates, same tolerance. |
| T3 | Sum phase | Phase of the summed response matches `AP2(Fc1)*AP2(Fc2)` within 2 degrees at 64 log-spaced points, at the default pair. |
| T4 | Static curve | 1 kHz sine, MID band only, threshold -20, ratio 4, attack 1 ms, release 50 ms, makeup 0. Measured steady-state output at inputs -40, -20, -10, 0 dBFS matches the formula in 8.2 within 0.5 dB. |
| T5 | Attack and release timing | 10 dB step above threshold. Gain reaches 63 percent of final within the specified attack, plus or minus 20 percent. Same for release, plus or minus 20 percent. |
| T6 | Band isolation | Sine at `Fc1` with LOW at 8:1 and MID/HIGH at 1:1. HIGH gain reduction is at most 0.5 dB. |
| T7 | Crossover change, no click | 1 kHz sine, `Fc1` stepped 80 to 300 Hz. Largest sample-to-sample delta is at most 1.5 times the steady-state delta of the sine. |
| T8 | Saturation off | `drive = 0`: HIGH band output is bit-identical to the unsaturated path. |
| T9 | Saturation on | `drive = 10`: output peak for a +12 dBFS input stays at or below 0 dBFS; small-signal gain is unity within 0.1 dB; total harmonic distortion of a 1 kHz sine is at least 10 percent. |
| T10 | Bypass null | `enabled = false` after settling: output equals the phase-aligned dry path with residual below -120 dBFS. |
| T11 | Mix 0 null | `mix = 0`: output equals the phase-aligned dry path with residual below -120 dBFS. |
| T12 | Mix 50 ripple | Magnitude at `mix = 50` has no dip deeper than 0.5 dB at `Fc1` and `Fc2` relative to the average of the neighbouring octave bands. |
| T13 | Finiteness | Sweep thresholds, ratios, attacks, releases, makeups, crossovers, drive and mix across their ranges, and input levels from -60 to 0 dBFS. No NaN, no infinity, no denormal growth. |
| T14 | Mono buffer | A one-channel buffer processes without touching a second channel, and produces the same result as channel 0 of a stereo buffer. |
| T15 | CPU ceiling | 10 seconds of stereo 48 kHz audio in at most 200 ms, reported as a fraction like the existing harness. |

### 11.2 Persistence tests

| ID | Test | Pass condition |
| :--- | :--- | :--- |
| P1 | Round trip | Encode a fully non-default settings object, decode, compare every field of every subtree. |
| P2 | Version 7 compatibility | A version 7 document without `<Multiband>` decodes with the module at defaults and everything else intact. |
| P3 | Version 8 without child | Rejected. |
| P4 | Version 8 with bad value | Rejected for out-of-range, non-finite, `cross1 >= cross2`, and `enabled = "2"`. |
| P5 | Version stability | Encoding a settings object that differs only in spread keeps the previous version number; encoding one with the module on produces version 8. |
| P6 | Fixture | `Tests/Fixtures/compatibility.xml` still loads, and re-encoding it produces the same version as before this change. |

### 11.3 UI tests

| ID | Test | Pass condition |
| :--- | :--- | :--- |
| U1 | Panel layout | Screenshot at 1660x940 and 1360x820: six tiles visible, `SCULPTOR` last, no clipped caption, no horizontal scrolling, no overlap with the output meter. |
| U1b | Tile states | On, off, hover, focus and disabled each render the states in 5.1.2; the LED tracks the module state across a rig load and a preset load. |
| U2 | Page opens | Gear click opens the page with the expected minimum width; escape closes it; a second click raises the existing window instead of opening another. |
| U3 | Band tabs | Switching tabs shows the other band's values and does not modify any value; the analyzer follows the tab only for its band caption. |
| U4 | Reset | Reset returns all controls to the defaults in section 7 and leaves ON and the active tab unchanged. |
| U5 | Null engine | The page opens and renders with no audio engine attached, matching the existing snapshot harness. |

### 11.4 Manual acceptance

1. Bass into a clean amp: LOW band at 4:1 with `Fc1` at 160 Hz tightens the low end without
   pumping.
2. Sweep `Fc1` and `Fc2` with a sustained low note: no click, no level jump, no notch.
3. Sweep dry/wet from 100 to 0 on a full-range signal: no comb filtering at intermediate
   values. This is the specific failure the phase-aligned dry path prevents.
4. Drive from 0 to 10 on a chord: grit appears, level stays roughly constant.
5. Bypass on and off repeatedly while playing: silent transition.

---

## 12. Implementation map (guidance, not a contract)

The team owns the final shape. This is the shortest path through the existing structure.

| File | Change |
| :--- | :--- |
| `Source/Effects/Multiband/MultibandParameters.h` | New. `Parameters`, defaults, `valid()`. Plain aggregate, no UI types. |
| `Source/Effects/Multiband/MultibandProcessor.h` | New. `Processor` with `prepare(rate)`, `setTarget(params, blockRate)`, `process(buffer)`, plus `gainReductionDb(int band)` and a scope tap. Header-only, like `AmpEqProcessor.h`. |
| `Source/Effects/Multiband/MultibandControls.h` | New. `popup::View` page, band tabs, knob mapping, help strings, IDs. |
| `Source/Effects/Multiband/MultibandAnalyzer.h` | New. FFT, log axis, band curves, tints, markers. |
| `Source/OutputDspParameters.h` | Add `multiband::Parameters multiband;`, extend `valid()`, add `requiresMultiband()`. |
| `Source/OutputDspState.h` | Version 8, child element, decode rules, and the six parent-version sites in 9.2. |
| `Source/OutputDspNodes.h` | `MultibandNode : NodeBase`, `ParameterMailbox<multiband::Parameters>`, atomics for level and gain reduction, scope ring. |
| `Source/Core/ProcessingGraph.h` | Add `output::MultibandNode* multiband` to `GraphStages`. |
| `Source/Core/ProcessingGraph.cpp` | Add the node between `eqNode` and `sp`, extend the chain array from 11 to 12 entries, configure it in `configureGuitarGraph`. |
| `Source/Modules/Play/UI/RackFxPanel.h` | New. Panel heading, six tiles, LED and icon painting, hover gear, focus ring. Replaces the four-name post-FX group in `ampui::paintChassis` if the panel is adopted (5.1.6). |
| `Source/Modules/Play/UI/RackFxIcons.h` | New. Vector icon paths for all six tiles, `SCULPTOR` included. |
| `Source/App/BoardMain.cpp` | Panel placement and sizing, tile wiring for the six existing items, dialog pointer, `showMultibandSettings()`, sync in `syncOutputUi()`, enable gating with `ready()`, help wiring. |
| `Source/CompactAmpLayout.h` | Only if the panel is deferred: `postFxStep` 120 to 96, fifth item, index shifts. |
| `Tests/MultibandTests.h` | New. T1 to T15, P1 to P6. |
| `Tests/OfflineTests.cpp` | `--multiband-test` dispatch. |
| `CMakeLists.txt` | `add_test(NAME ToneGrid.Multiband ...)`. |

No new binary assets. The page is drawn in code with `popup::Look`, so it inherits the
application style and needs no artwork pipeline, no provenance entry and no manifest row.

---

## 13. Delivery plan

| Phase | Content | Exit condition |
| :--- | :--- | :--- |
| P1 | DSP engine and T1 to T15 | `--multiband-test` passes at 44100, 48000 and 96000 |
| P2 | Node, graph insertion, persistence, P1 to P6 | Export path renders with the module; rig round trip passes; compatibility fixture unchanged |
| P3 | Panel tile, page, analyzer, meter, U1 to U5 | Screenshots at both window sizes; no click on bypass, crossover or reset |
| P4 | Help strings, tooltips, release note, downgrade warning | Review sign-off |

Definition of done:

- `ctest` green locally on macOS arm64; the existing suites unchanged.
- The four existing rack effects still pass their tests, which guards against the decode
  defects in 9.2.
- No new compiler warnings under the project's existing warning settings.
- Release note states that version 8 rigs cannot be opened by earlier builds.
- The module is off in every shipped preset and in a fresh rig.

---

## 14. Risks and open questions

Risks:

1. **The six parent-version sites in the codec.** High probability of a partial update. The
   P1 regression test is the mitigation; do not merge P2 without it.
2. **Panel scope creep.** The reference panel adds an EQ tile and drops the PITCH tile.
   Treating that as part of this feature risks a UI refactor inside an audio feature. The
   mitigation is 5.1.6: the panel is presentation only, every existing item keeps its current
   behaviour, and the issue records whether the panel ships here or later.
3. **Tile legibility.** Six tiles at 1360 wide leave about 175 pixels each. The caption size
   scales with the tile, so `SCULPTOR` is the fit reference; if it clips, reduce the letter
   spacing before reducing the font size.
4. **Stereo linking.** Linked detection changes the result compared with two independent
   channels. It is the correct choice for a bus compressor, but the null test in T14 must
   confirm that dual-mono input is unaffected.
5. **Analyzer cost on low-end machines.** 30 frames per second with a 2048-point FFT is
   cheap, but the filled path drawing is not. Cap the points at 256 per curve and skip
   repaint when the popup is not visible.
6. **Downgrade.** Version 8 blocks older builds. This is existing policy for every version
   bump, but it must be said in the release note because affected users cannot roll back a
   saved rig.

Open questions, with recommendations:

1. Should a preset be able to set the module? **Recommendation: no for v1.** It would need a
   ToneBank schema bump and 182 preset edits for a feature most presets would leave off.
2. Is the RACK FX panel part of this branch or a separate one? **Recommendation: same branch.**
   The tile is the only entry point in the reference design, and a temporary fifth item in the
   old strip produces screenshots that no reviewer will accept as done.
3. Should the tile click open the page instead of toggling? **Recommendation: toggle.** It
   matches the existing rack items, where the LED is the on/off control, and it keeps a
   single click for the common action.
4. Is linked stereo detection right, or should each channel be independent?
   **Recommendation: linked.** The image is more stable and the CPU is halved.
5. Should the meter show the maximum across bands, or the sum? **Recommendation: maximum.**
   Summing understates the reduction in any one band.
6. Should the analyzer top out at 10 kHz as drawn, or 16 kHz as specified?
   **Recommendation: 16 kHz.** It costs nothing on a log axis and shows the high band's
   saturation harmonics.

---

## Appendix A: what changed from the earlier draft

The earlier draft of this document planned a fourteenth pedal. That plan is withdrawn.

| Earlier draft | Now | Why |
| :--- | :--- | :--- |
| `PedalId::LowEndSculptor = 13`, appended to `definitions` | Rack FX node, no pedal | Avoids the 7+6 to 7+7 rack rewrite, the 14th skin asset, the preset-count arrays, and the 182-preset ToneBank question entirely |
| Rig format 18, `<Pedal id="13">` | `<OutputDSP version="8">` | Rack FX already have a versioned, backward-compatible home |
| `board::Processor` forces `mix = 1` for this pedal | Not needed | The module owns its own dry/wet and never touches `Setting.mix` |
| Three face knobs plus an advanced panel | Full page with five knobs per band | A popup page has no three-control limit, so the reference design is buildable as drawn |
| Gain reduction exposed but unused | Meter, per band and combined | The reference design makes the meter a first-class element |

Everything below is unchanged from the earlier analysis because it was verified against the
source: the platform constraints in section 2, the zero-latency rule, the 48 kHz engine, the
test harness shape, and the crossover construction.

---

## Appendix B: verified crossover numbers

Measured with a cascade biquad simulation at 48 kHz. Band paths were evaluated as cascaded
second-order sections, never as one expanded polynomial.

| Crossover pair | Naive construction (LP4 / HP4 / LP4+HP4) | Construction in 8.1 |
| :--- | :--- | :--- |
| 80 / 800 Hz | 0.118 dB error | 0.0000000 dB |
| 160 / 1800 Hz | 0.092 dB error | 0.0000000 dB |
| 350 / 4000 Hz | 0.085 dB error | 0.0000000 dB |
| 350 / 800 Hz | 5.12 dB error | 0.0000000 dB |
| 200 / 400 Hz | 7.95 dB error | 0.0000000 dB |

Two measurement rules for the implementer:

1. Evaluate the bands as cascaded biquad sections. Expanding the product into a single
   eighth-order polynomial and evaluating it at the crossover frequency loses precision
   through cancellation and reports errors of tens of dB that do not exist.
2. Measure an impulse response and take its FFT, rather than sweeping single tones. A sweep
   with a coarse step can step over a narrow error and pass a broken crossover.
