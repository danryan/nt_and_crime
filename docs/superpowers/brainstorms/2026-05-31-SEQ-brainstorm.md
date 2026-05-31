# Brainstorm: SEQ (Sequins) NT plug-in

Date: 2026-05-31
Status: shipped
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #52

## Scope

Port vendor `APP_SEQ.h` (Sequins: a dual-channel step sequencer) to one NT
plug-in `.o` under `plugins/apps/`, on the shared branch `dr/oc-apps-port-2`. This
is the capstone of the issue #36 O_C-app port track.

## Categorization

Multi-channel app (the DQ shape): `NUM_CHANNELS == 2`, the file-scope array
`seq_channel[2]`, each a `SettingsBase<SEQ_Channel, SEQ_CHANNEL_SETTING_LAST>`,
plus a separate `seq_state` (SEQ_State) holding the customUI state (the pattern
editor, the scale editor, the menu cursor). Channel 0 drives DAC A (main pitch) +
C (aux envelope); channel 1 drives DAC B + D. The two clocks are TR1 / TR3.

This is the DQ multi-channel-facade template with a per-channel setting subset:

- Five contiguous U16 masks at indices 10..14 overflow int16 and are excluded:
  `SEQ_CHANNEL_SETTING_SCALE_MASK` (10) plus the four sequence masks
  `MASK1..MASK4` (11..14), all `STORAGE_TYPE_U16` with range up to 65535. Edited
  through the scale editor (SCALE_MASK) and the pattern editor (MASK1..4). The
  within-channel remap maps logical row w to physical `w < 10 ? w : w + 5`.
  `SEQ_CHANNEL_SETTING_LAST == 58`, so 53 exposed rows per channel, 106 flat NT
  rows.
- The channels live in a SEPARATE array (not the facade's own SettingsBase), so
  the facade `save`/`restore`/`storage_size` are overridden to the whole-app
  `SEQ_save`/`SEQ_restore`/`SEQ_storageSize` (the AUTOMATONNETZ/DQ/QQ blob
  override) or the masks (and everything else) would not persist.
- Two vendor UI editors: `OC::PatternEditor<SEQ_Channel>` AND
  `OC::ScaleEditor<SEQ_Channel>`. The handleButtonEvent and both editors read
  `EVENT_BUTTON_LONG_PRESS`, so dispatch is `<true>`.

## HSIOFrame blocker (covered by the existing provisioning patch)

`APP_SEQ.h` `#include "HSIOFrame.h"` (a Hemisphere header it uses nothing from),
the same vestigial include QQ has. The existing
`vendor-patches/0001-oc-apps-drop-vestigial-hsioframe.patch` already strips it
from BOTH `APP_QQ.h` and `APP_SEQ.h`, so SEQ is unblocked with no new patch. The
patch is applied to the vendor working tree by `bootstrap.sh`; the pin SHA is
unchanged.

## Risks (all resolved)

- The vendor `APP_SEQ.h` directly `#include "OC_ui.h"`, which (resolved same-dir
  to the vendor copy) hard-includes the whole non-portable UI chain. The shim
  `OC_ui.h` used `#pragma once`, not the named guard, so it did not suppress the
  vendor body. Fix: poison `OC_UI_H_` in the shim `OC_ui.h` (the established
  include-guard-poison lesson). SEQ is the first OC app whose vendor header pulls
  vendor `OC_ui.h`.
- `OC::Strings::seq_playmodes` (the Playmode enum labels) was not shim-owned.
- `OC::ADC::smoothed_raw_value`, `menu::SettingsListItem::Draw_PW_Value_Char`, and
  `weegfx::Graphics::print(int, unsigned width)` were not shim-owned. Mechanical
  hand-ports.
- SlewedValue / randomSeed: already shim-owned (ASR). The .cpp must
  `#include "util/util_math.h"` explicitly before the vendor header (the ARM build
  does not force-include the shadow). `multiply_u32xu32_rshift32` comes from the
  vendor `extern/dspinst.h` SEQ already includes (not the shim util_math, which
  deliberately omits it; the QQ lesson).

## Exclusions

None beyond the five U16 mask subset per channel.

## Out of scope

Nothing. SEQ is the last app in issue #36.
