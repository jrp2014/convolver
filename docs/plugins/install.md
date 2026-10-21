<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Install a developer preview

Portable Convolver archives contain `VST3/Convolver.vst3`, `bin/`, `presets/`,
`docs/` and `licenses/`. Keep the complete VST3 bundle. These are unsigned developer
previews, with no production OS/host qualification or published release yet.

Copy the bundle to your host's VST3 search location:

- Windows: `%COMMONPROGRAMFILES%\VST3\Convolver.vst3` (administrator access).
- macOS: `~/Library/Audio/Plug-Ins/VST3/Convolver.vst3` for your account.
- Linux: `~/.vst3/Convolver.vst3` for your account.

Keep the tools, examples and guide together in a separate directory. Windows
builds require the Microsoft Visual C++ 2015–2022 x64 runtime. Do not place FFTW
DLLs in a host directory.

## Unsigned previews

Previews are not signed with an Apple Developer ID or a Windows code-signing
certificate yet. Check the package's SHA-256 against the candidate manifest
before going further.

- **macOS:** the bundle carries only an ad-hoc signature (Apple Silicon needs at
  least that). Files downloaded with a browser are quarantined, and hosts may
  refuse to load a quarantined unsigned bundle. After copying it, clear the
  quarantine for this bundle only:

  ```sh
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Convolver.vst3
  ```

  Do the same for `bin/convolver-vst3-preset` if you use the converter. This
  changes nothing outside those files; do not turn off Gatekeeper system-wide.
- **Windows:** an unsigned installer shows a SmartScreen warning ("Windows
  protected your PC"). Choose **More info**, check that the publisher is
  "Unknown publisher" and the file name matches, then **Run anyway**. Copying the
  bundle from the ZIP shows no warning.

## Replacing and removing

Close hosts before replacing the bundle. Preserve project backups and custom presets.
To remove an archive installation, remove only the bundle you copied and the
extracted package directory. Host projects and user-created presets are yours.
See [the quickstart](quickstart.md) after your host rescans.

Preview installers are also produced for Windows and macOS, but their real-machine
installation and trust paths remain unqualified. Windows installs the plug-in
for all users and tools in the selected Convolver directory; upgrading removes
the previous owned files before replacing them. Close hosts before uninstalling;
a locked file causes failure and leaves the uninstaller available for retry.
macOS packages install into `/Library/Audio/Plug-Ins/VST3` and
`/Library/Application Support/Convolver`; the latter contains `uninstall.sh`.
Run that script with administrator privileges only for an installation you intend
to remove. Both uninstallers preserve unrelated files and custom presets.
