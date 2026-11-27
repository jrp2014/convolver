<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-61

## Fixed

- Audio Unit (PLUG-61): when you change Latency while Status shows Load
  failed, a fault in the first blocks the new latency plays no longer
  shows Fault in its place for a moment; Load failed stays, as it already
  did in VST3 and LV2. See
  [status and the Convolver log](../docs/use/plugins.md#status-and-the-convolver-log).
