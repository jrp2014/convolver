<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Report a problem

For anyone trying a Convolver preview who found something wrong, or something
hard to follow. It says what to collect on macOS, Windows and Linux, and
where to send it.

**In short:**

- Open an issue on [GitHub](https://github.com/jrp2014/convolver/issues/new/choose):
  **Problem report** for a crash, wrong sound or refused filter, **Preview
  feedback** for what worked, what was confusing or what you missed. You
  need a free GitHub account.
- On macOS, press **Copy Diagnostics** in Convolver's window and paste it
  into the report. On Windows and Linux, where the plug-in has no window,
  attach the Convolver log file instead.
- Say which system, host and version you used, how you loaded the filter,
  and whether the problem is in playback, an export or a reopened project.

**Status:** Convolver is a developer preview, and reports from people
trying it decide what is fixed next. The [status page](../project/status.md)
says what is ready and what has not been checked yet, and
[limits](limits.md) what Convolver does not do.

## 1. Note what happened

Write down, while you remember them:

- What you did, what you expected and what happened instead.
- How the filter was loaded: **Choose…**, **Recent** or **Reload** in
  Convolver's window (macOS), a preset from the host's preset menu, or a
  reopened project.
- Where the problem shows: in what you hear while playing, in an export or
  render, or after reopening the project.
- The host and its version (for example REAPER 7.2), your system and its
  version, and the processor (for example macOS 26.1 on Apple M2, or
  Windows 11 on x64).

## 2. Collect Convolver's messages

Convolver explains every failure in the
[Convolver log](plugins.md#status-and-the-convolver-log), a text file with
one line for each load and failure. Each line names the Convolver version,
so the log also says which version you ran.

### macOS

In Convolver's window, open **File, software and log** and press
**Copy Diagnostics**. It copies the filter's details, the software versions
and this instance's recent log lines as text, ready to paste.

If the window will not open, copy the whole log in Terminal instead:

```sh
pbcopy < ~/Library/Logs/Convolver/Convolver.log
```

GarageBand and other sandboxed hosts keep the log inside their own folder.
Copy the last hour's lines from the system log instead:

```sh
log show --last 1h --info --predicate 'eventMessage CONTAINS "Convolver"' --style compact | pbcopy
```

### Windows

Press **Windows+R**, type `%LOCALAPPDATA%\Convolver` and press Enter. Attach
`Convolver.log` from the folder that opens to the report, or copy it in
PowerShell:

```powershell
Get-Content "$env:LOCALAPPDATA\Convolver\Convolver.log" | Set-Clipboard
```

Also copy the plug-in's **Status** text, if your host shows it in its
generic plug-in window or parameter list.

### Linux

Attach `~/.local/state/convolver/convolver.log` to the report (under
`$XDG_STATE_HOME` if you set it), or copy it with `wl-copy` (Wayland) or
`xclip` (X11):

```sh
wl-copy < ~/.local/state/convolver/convolver.log
xclip -selection clipboard < ~/.local/state/convolver/convolver.log
```

Also copy the plug-in's **Status** text, if your host shows it. The LV2
plug-in has no file of its own: copy the host's log instead.

### The command-line tools

Copy the whole message the tool printed, and the command you ran. Run
`./bin/convolver --version` from the package folder for the version.

## 3. Open the issue

1. Go to [new issue](https://github.com/jrp2014/convolver/issues/new/choose)
   and choose **Problem report** or **Preview feedback**.
2. Fill in the form with what you noted in step 1.
3. Paste Convolver's messages into **Messages**, or drag the log file onto
   the form to attach it.
4. Add the filter if it is small: the `.cfg` file, or the IR's channels,
   length and sample rate. The bundled examples in `presets/` and
   `examples/first-use/` are best, if they show the problem too.

**Keep private things out.** The log and Copy Diagnostics name file paths,
which can include your user name and folder names; edit them before you
paste if you prefer. Do not attach private recordings or session files.
Keep levels low when you try unfamiliar IRs.

**Check it worked:** the issue appears in the
[list of issues](https://github.com/jrp2014/convolver/issues) with your
form's answers. jrp, the maintainer, may ask for more details there, so keep
notifications on for it.
