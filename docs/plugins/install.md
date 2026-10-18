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
DLLs in a host directory. macOS may reject ad-hoc builds under its signature policy;
use a properly signed release when available, and do not disable system security.

Close hosts before replacing the bundle. Preserve project backups and custom presets.
To remove an archive installation, remove only the bundle you copied and the
extracted package directory. Host projects and user-created presets are yours.
See [the quickstart](quickstart.md) after your host rescans.
