---
name: Électronique Métrologique 122x250
colors:
  surface: '#faf9f5'
  surface-dim: '#dadad6'
  surface-bright: '#faf9f5'
  surface-container-lowest: '#ffffff'
  surface-container-low: '#f4f4f0'
  surface-container: '#eeeeea'
  surface-container-high: '#e8e8e4'
  surface-container-highest: '#e2e3df'
  on-surface: '#1a1c1a'
  on-surface-variant: '#4c4546'
  inverse-surface: '#2f312e'
  inverse-on-surface: '#f1f1ed'
  outline: '#7e7576'
  outline-variant: '#cfc4c5'
  surface-tint: '#5e5e5e'
  primary: '#000000'
  on-primary: '#ffffff'
  primary-container: '#1b1b1b'
  on-primary-container: '#848484'
  inverse-primary: '#c6c6c6'
  secondary: '#5e5e5e'
  on-secondary: '#ffffff'
  secondary-container: '#e2e2e2'
  on-secondary-container: '#646464'
  tertiary: '#000000'
  on-tertiary: '#ffffff'
  tertiary-container: '#1b1b1b'
  on-tertiary-container: '#848484'
  error: '#ba1a1a'
  on-error: '#ffffff'
  error-container: '#ffdad6'
  on-error-container: '#93000a'
  primary-fixed: '#e2e2e2'
  primary-fixed-dim: '#c6c6c6'
  on-primary-fixed: '#1b1b1b'
  on-primary-fixed-variant: '#474747'
  secondary-fixed: '#e2e2e2'
  secondary-fixed-dim: '#c6c6c6'
  on-secondary-fixed: '#1b1b1b'
  on-secondary-fixed-variant: '#474747'
  tertiary-fixed: '#e2e2e2'
  tertiary-fixed-dim: '#c6c6c6'
  on-tertiary-fixed: '#1b1b1b'
  on-tertiary-fixed-variant: '#474747'
  background: '#faf9f5'
  on-background: '#1a1c1a'
  surface-variant: '#e2e3df'
typography:
  headline-lg:
    fontFamily: JetBrains Mono
    fontSize: 28px
    fontWeight: '800'
    lineHeight: 28px
    letterSpacing: -0.5px
  headline-md:
    fontFamily: JetBrains Mono
    fontSize: 18px
    fontWeight: '700'
    lineHeight: 20px
    letterSpacing: 0px
  headline-sm:
    fontFamily: JetBrains Mono
    fontSize: 13px
    fontWeight: '700'
    lineHeight: 14px
    letterSpacing: 0.5px
  body-lg:
    fontFamily: JetBrains Mono
    fontSize: 11px
    fontWeight: '600'
    lineHeight: 13px
    letterSpacing: 0px
  body-md:
    fontFamily: JetBrains Mono
    fontSize: 10px
    fontWeight: '500'
    lineHeight: 12px
    letterSpacing: 0px
  body-sm:
    fontFamily: JetBrains Mono
    fontSize: 9px
    fontWeight: '500'
    lineHeight: 11px
    letterSpacing: 0px
  label-lg:
    fontFamily: JetBrains Mono
    fontSize: 9px
    fontWeight: '700'
    lineHeight: 10px
    letterSpacing: 0.75px
  label-md:
    fontFamily: JetBrains Mono
    fontSize: 8px
    fontWeight: '700'
    lineHeight: 9px
    letterSpacing: 0.5px
  label-sm:
    fontFamily: JetBrains Mono
    fontSize: 7px
    fontWeight: '700'
    lineHeight: 8px
    letterSpacing: 0.5px
spacing:
  gutter: 4px
  margin: 2px
  space-xs: 2px
  space-sm: 4px
  space-md: 6px
  space-lg: 8px
  space-xl: 12px
---

## Brand & Style

This design system defines an ultra-specialized, mission-critical interface engineered for the TTGO T5 ESP32 2.13-inch vertical electronic paper display (native resolution: 122 × 250 pixels). The system adopts an unyielding Industrial-Brutalist & Metrological aesthetic rooted in precision European instrumentation, rugged laboratory apparatus, and specialized field diagnostics.

The target audience comprises technical field operators, metrology technicians, and environmental monitoring specialists working in outdoor or harsh indoor environments where ambient glare, low refresh cycles, and zero-power bistable rendering dictate UI rules. The emotional response is one of total operational certainty: raw, calculated, indestructible, and unmistakably functional.

All visual grammar prioritizes 1-bit raster legibility. Anti-aliasing, intermediate greys, dropshadows, blurs, and soft radius curves are strictly forbidden within the digital viewport. Hierarchy is established exclusively through black-and-white field inversion, solid 1px and 2px dividing rules, high-density typographic weight shifts, and rigid coordinate plotting. Surrounding the display, the hardware casing balances this severe 1-bit digital viewport with a purposeful, matte industrial enclosure featuring softly radiused mechanical tooling.

## Colors

The system uses an uncompromising two-state color architecture formulated specifically for micro-encapsulated electrophoretic displays (e-ink / e-paper):

- **Display Field (Papier)**: `#f4f4f0` — Off-white, slightly warm electrophoretic substrate base. Simulates natural uncharged particle states while reducing harsh full-spectrum glare.
- **Active Ink (Encre)**: `#000000` — Deep, pure carbon black. Used for all glyphs, vector lines, borders, active bitmap glyphs, and inverted selection regions.
- **Hardware Enclosure (Boîtier)**: `#e2e3de` — Structural ABS matte chassis framing the active panel.
- **Hardware Highlight**: `#f9f9f7` — Beveled bezel boundary and tactile surface sheen.

Color variations, simulated half-tones, and CSS opacity filters below 100% are banned. To express hierarchy or active states, surfaces invert completely: `#000000` becomes the background slab and `#f4f4f0` becomes the cutout glyph/icon color. Dithering (Bayer 2×2 or checkerboard 1-bit stippling) is permitted solely for passive progress bars or inactive scale registers.

## Typography

Typography is locked to `JetBrains Mono` to guarantee monospaced predictability, tight horizontal metrics, wide apertures, and unmistakable glyph distinction on low-pixel budgets. 

Rules of composition:
- **Language**: French technical nomenclature. Headers and status designations are displayed in full uppercase (`ÉTAT`, `MESURE`, `RÉSEAU`, `BATTERIE`, `HISTORIQUE`). Diacritical marks must remain crisp; where vertical clearance is limited to 8px, abbreviated upper-case tokens may be used (`CONF.`, `PARAM.`, `TEMP.`).
- **Numerical Dominance**: The primary readout (`headline-lg`, 28px/800 weight) dominates the viewport center, allowing immediate glanceability from a 1.5-meter distance.
- **Labels & Micro-data**: Fixed-pitch mono labels enforce strict vertical alignment of decimals and units (`V`, `mA`, `hPa`, `°C`, `ppm`).
- **Rendering fidelity**: Always render with font-smoothing disabled or crisp pixel hinting to avoid partial-alpha grey fringing along curved stems.

## Layout & Spacing

The canvas is rigidly locked to an exact physical frame of **122 pixels wide by 250 pixels tall**. No responsive resizing or dynamic reflow is allowed. Every interface element is positioned using integer coordinates on this pixel grid.

### Vertical Zoning (250px Total Budget)
1. **Header Zone (`Y: 0 → 15`, height: 16px)**:
   - Full-bleed inverted solid black banner (`#000000`).
   - Displays station identifier, connection icon/status, and battery voltage indicator in `#f4f4f0`.
2. **Subheader / Mode Strip (`Y: 16 → 27`, height: 12px)**:
   - Categorical label and measurement mode in uppercase (`label-md`). Bottom border: 1px solid black.
3. **Primary Metric Hero (`Y: 28 → 95`, height: 68px)**:
   - Dedicated readout chamber. Hosts large values (28px weight), units of measure, and delta variance indicator.
4. **Secondary Data Matrix (`Y: 96 → 165`, height: 70px)**:
   - Two-column or three-row grid partitioned by 1px solid horizontal and vertical rules. Displays auxiliary telemetry (e.g., `MIN`, `MAX`, `MOY`).
5. **Graphic Register / Sparkline (`Y: 166 → 217`, height: 52px)**:
   - 1-bit point or stepped bar array representing historical metrics or status checklists.
6. **Interaction Bar / Footer (`Y: 218 → 249`, height: 32px)**:
   - Fixed tactile prompt zone. Top border: 2px solid black. Instructs the user on single-button navigation.

Margins are set strictly to 2px inner padding to avoid active e-paper gate lines at the physical panel edge. Gutters between dual data columns are fixed at 4px.

## Elevation & Depth

True physical depth does not exist on a bistable 1-bit display. Elevation and visual prominence are achieved exclusively through structural zoning, high-contrast borders, and polarity inversion:

- **Surface Level 0 (Base Display Surface)**: Raw electrophoretic paper `#f4f4f0`. All default metrics and data grids sit directly on this level.
- **Surface Level 1 (Framed Containers)**: Bounded regions enclosed with a continuous 1px solid `#000000` rule. Used for secondary cells and data grouping.
- **Surface Level 2 (Selected / Inverted Blocks)**: Full solid `#000000` background fill with cut-out `#f4f4f0` text and icons. Used for active modal focus, urgent warnings, or currently targeted menu rows.
- **No Drop Shadows, No Blurs, No Gradients**: Any gradient results in uncontrolled physical e-ink ghosting and edge flutter. Transitions between states must be binary and abrupt.

## Shapes

The digital coordinate space enforces absolute zero-radius geometry (`roundedness: 0`). 
- Every screen-drawn box, tag, progress track, list container, and inverted field terminates in strict 90-degree squared corners.
- Circular or diagonal geometries are accepted solely for native measurement glyphs (such as degree signs `°`, battery tip caps, or circular target indicators).

### Physical Enclosure Harmony
While the digital screen is relentlessly sharp and unyielding, the enclosing plastic chassis uses subtle mechanical radii (`3mm` to `4mm` on exterior casing corners) to provide pocketability and structural impact resistance. This juxtaposition emphasizes the precision glass instrument framed safely within a durable protective tool.

## Components

### 1. Navigation & Interaction Footer (Single-Button Grammar)
Because the TTGO T5 features only one physical hardware button, all interactions are mapped to two fundamental inputs:
- **Court (`●`)**: Short press (< 400ms). Used for cycle, increment, step forward (`SUIVANT`).
- **Long (`—`)**: Long press / hold (> 1000ms). Used for confirmation, entry, reset (`VALIDER` / `ENTRER`).

The footer component is a permanent 32px-high block anchored at the bottom edge (`Y: 218–249`):
- **Divider**: Top edge bounded by a solid 2px black stroke.
- **Content**: Split horizontally or vertically into clear command directives:
  - Top row: `[●] DÉFILER`
  - Bottom row: `[—] CONFIRMER`
- **Active execution**: During long press detection, a 2px horizontal solid black line fills from left to right across the bottom 3px of the screen to provide physical hold feedback.

### 2. Header Status Strip
- **Height**: 16px. Solid `#000000` fill.
- **Left**: Device state identifier (e.g., `TTGO-METR01` or `CH01`).
- **Center**: Acquisition status (`ACQ`, `PAUSE`, `ERREUR`).
- **Right**: 1-bit battery glyph (12×7px outline with interior segmented charge fill) accompanied by percentage text in 8px bold mono (`94%`).

### 3. Metric Value Display (Hero Unit)
- **Container**: 118px wide × 48px high.
- **Primary Value**: Large numerals (28px) aligned left or center-left.
- **Unit Badge**: Affixed to the upper-right of the integer (`V`, `hPa`, `PPM`, `°C`) in 10px bold text.
- **Trend Indicator**: Compact 7×7px 1-bit direction arrows (`▲`, `▼`, `■`) marking delta relative to preceding cycle.

### 4. Menus & Selection Lists
- **Row Height**: 16px per option.
- **Standard Row**: `#f4f4f0` background, `#000000` text, prefixed with empty bracket marker `[ ]`.
- **Selected Row**: Inverted `#000000` background slab extending full width (118px), text knocked out in `#f4f4f0`, prefixed with solid bracket marker `[X]` or filled square `[■]`.

### 5. Progress & Level Gauges
- **Dimensions**: 118px wide × 8px high.
- **Frame**: 1px solid black bounding rectangle.
- **Fill**: Solid black fill for linear progress. Segmented scales use 2px vertical dividers every 10% increment.
- **Dither Variant**: A 50% 1-bit checkerboard fill designates safe operating threshold limits or non-critical zones.

### 6. Hardware Enclosure Frame & Physical Button Indicator
- **Enclosure Dimension**: Visual representation of the hardware frame around the 122×250 viewport.
- **Frame Width**: Minimum 8px border on sides, 12px at top, 24px at bottom.
- **Material Tone**: Matte industrial pale gray/beige (`#e2e3de`).
- **Tactile Switch Cue**: Centered 6px below the display's bottom edge sits an engraved circular button icon (`Ø 10px`) debossed with an etched concentric circle and labeled `FUNC` or `SÉLEC`.