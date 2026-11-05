<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Trademarks

The VST trademark wording and logo that Convolver's documentation, packages
and plug-in credits carry (PLUG-5). jrp approved the wording on
5 November 2026 ([decisions](../../docs/project/decisions.md#releases)).

## The wording

[`vst.json`](vst.json) is the one source of the text:

- "Convolver supports VST® 3."
- "VST is a registered trademark of Steinberg Media Technologies GmbH."

Steinberg's [usage guidelines](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST%2B3%2BLicensing/Usage%2Bguidelines.html)
ask for ® on the first use of "VST" in any product-related material, the
trademark sentence in all product credits and documentation, and the VST
Compatible logo in all product documentation and on every web page that
refers to the product.

Where it goes, and what keeps it there:

| Where | What | Written and checked by |
| --- | --- | --- |
| `README.md` and every page in `docs/use/` that mentions VST (all of them ship in packages) | ® on the first mention of VST; at the end, the logo and both sentences | `python3 scripts/trademarks.py` checks (pre-commit runs it); `--fix` writes the closing block |
| The VST3 bundle's `Contents/Resources/NOTICE.txt` (the plug-in's credits) | Both sentences | CMake, from `vst.json` (`plugins/vst3/CMakeLists.txt`); `release.stage` and the candidates check it |
| The macOS installer's choices (`packaging/macos/Distribution.xml`) | ® on the first mention; the trademark sentence | `scripts/trademarks.py` checks |

To change the wording, edit `vst.json`, run `python3 scripts/trademarks.py --fix`
and record jrp's decision in the decisions log.

## The logo

[`docs/use/images/VST_Compatible_Logo_Steinberg.png`](../../docs/use/images/VST_Compatible_Logo_Steinberg.png)
is Steinberg's official VST Compatible logo, byte for byte as published, from
the VST 3 SDK's documentation repository
([steinbergmedia/vst3_doc](https://github.com/steinbergmedia/vst3_doc),
`artwork/VST_Compatible_Logo_Steinberg.png` at tag `v3.8.1_build_84`, commit
`8bfca19d3b76a61d093951ba9297047f544caea1`; the folder the guidelines name as
`VST_SDK/VST3_SDK/vst3_doc/artwork`). Its SHA-256 is recorded in `vst.json`,
and `scripts/trademarks.py` fails if the file changes. That repository is
under the MIT licence, Copyright (c) 2026 Steinberg Media Technologies GmbH,
the same notice as the VST 3 SDK's (`licenses/vst3sdk.txt` in packages).

Pages show it 120 pixels wide (about 32 mm on a 96 dpi screen), above the
guidelines' minimum of 15 × 10 mm, and never recoloured, cropped or combined
with Convolver's name; do not edit the file. The packaged guides keep their
relative link to it, since packages ship `docs/use/images/`.
