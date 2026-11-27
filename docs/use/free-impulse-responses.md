<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Free impulse responses to try

[Using Convolver](README.md) · [Configuration files](configuration.md)

For anyone who wants a reverb to try in Convolver: free collections of
impulse responses (IRs) recorded in real spaces, what their files hold, and
how to play each one.

**In short:**

- Convolver brings no reverbs of its own. The collections below are free to
  download; each sets its own licence, so read it before using the IRs in
  work you publish or sell.
- Check a file before you play it: `convolver --inspect file.wav` prints its
  sample rate and channel count.
- A file plays only at its own sample rate. Run the host or render at that
  rate, or convert the IR file to your rate first in a sound editor.
- A mono or stereo file plays directly, and a four-channel true-stereo file
  plays directly on stereo audio
  ([mono and true-stereo IR files](configuration.md#mono-and-true-stereo-ir-files)).
- **Do not play OpenAIR's B-format files directly.** They and other
  Ambisonics B-format files are four-channel, but not true stereo: on a
  stereo track Convolver plays an unmarked one as true stereo, and it
  sounds wrong. [Play its W channel](#play-a-b-format-file-as-a-mono-reverb)
  instead.
- Measured reverbs are often much louder than the dry sound. Choose the
  Gain mode **Estimated** in a plug-in, or render with `--gain-db auto`.
  It is an estimate, not a guarantee: if the output still clips, lower the
  gain. **Clip-safe** (`--gain-db no-clip`) never clips but leaves a long
  reverb very quiet ([gain mode](plugins.md#settings)).

## Choose a collection for music or sound design

| Collection | What it holds | Files | Licence |
| --- | --- | --- | --- |
| [OpenAIR](https://www.openair.hosted.york.ac.uk/) (University of York) | Concert halls, churches, theatres and other spaces; each space has its own page with its IRs to download | Many spaces are B-format: one four-channel WAV file for each position, at 48 or 96 kHz in the spaces checked. Do not play these directly: use the [W channel recipe](#play-a-b-format-file-as-a-mono-reverb) | Set on each space's page; most are Creative Commons, such as CC BY 4.0 |
| [EchoThief](https://www.echothief.com/downloads/) | Over a hundred unusual places in North America, such as caves, tunnels, bridges and underpasses | One zip | Copyright Chris Warren; the site asks commercial users to contact it for a licence |
| [MIT IR Survey](https://mcdermottlab.mit.edu/Reverb/IR_Survey.html) | 271 responses of everyday places, chosen by where volunteers spent their days; [one zip](https://mcdermottlab.mit.edu/Reverb/IRMAudio/Audio.zip) | Recorded 1.5 m from the source | None stated on the page; cite the 2016 paper it names |
| [Voxengo free impulses](https://www.voxengo.com/impulses/) | 38 halls, rooms, other spaces and speaker cabinets, modelled with Voxengo's Impulse Modeler, not recorded; [one zip](https://www.voxengo.com/files/impulses/IMreverbs.zip) | 44.1 kHz, 16-bit WAV | Free to use, commercially too; may be passed on only unchanged and free of charge |
| [Queen Mary C4DM room impulse responses](https://isophonics.net/content/room-impulse-response-data-set.html) | A great hall, an octagonal hall and a classroom, each measured at 130 or 169 positions | 96 kHz, 32-bit WAV, one mono file per microphone channel: omnidirectional, and B-format W, X, Y and Z in separate zips | Creative Commons Attribution-NonCommercial-ShareAlike, credited to the Centre for Digital Music, Queen Mary, University of London |

Where a row gives no sample rate or channel count, the collection's pages
do not state it:
check each file with `--inspect`.

For a first try, the C4DM omnidirectional files play directly as a mono
reverb on any number of channels, at 96 kHz. An OpenAIR space needs the
[W channel recipe](#play-a-b-format-file-as-a-mono-reverb) unless its page
offers a mono or stereo file.

## Choose a research database

These are made for speech research and are large downloads. BUT's 16 kHz
files fit only 16 kHz audio:

| Database | What it holds | Files | Licence |
| --- | --- | --- | --- |
| [Aachen Impulse Response Database](https://www.iks.rwth-aachen.de/en/research/tools-downloads/databases/aachen-impulse-response-database) (RWTH Aachen) | Rooms measured with a dummy head, two ears per position, and phone positions; one 175 MB zip | MATLAB files, with WAV versions included | MIT licence, in the zip |
| [BUT ReverbDB](https://speech.fit.vut.cz/software/but-speech-fit-reverb-database) (Brno University of Technology) | Nine rooms, 31 microphones each; the IR-only download is 8.7 GB | 16 kHz, 16-bit mono WAV, one file per microphone | CC BY 4.0, says the download page; its readme carries an Apache 2.0 notice |

A two-ear (binaural) response is for headphones and a mono source. Play a
two-channel one on mono audio with a `.cfg` that has one input and two
outputs, one path per ear; change the first number to the file's rate:

```text
// sample rate, input channels, output channels, speaker mask
48000 1 2 3
// input delay in milliseconds
0
// output delays in milliseconds
0 0
// left ear
brir.wav
0
0
0
// right ear
brir.wav
1
0
1
```

It takes one input channel, so it is refused on stereo audio: mix the
source to mono first.

## Play a B-format file as a mono reverb

B-format is Ambisonics: channel W is an omnidirectional microphone, and X, Y
and Z are figure-of-eight microphones facing three ways. OpenAIR's
four-channel files are B-format. A plain
four-channel WAV file does not say that it is B-format, so on stereo audio
Convolver plays it as true stereo, and it sounds wrong; a file whose
header marks it as B-format is refused
([not every four-channel file is true stereo](configuration.md#mono-and-true-stereo-ir-files)).
Either way, the `.cfg` below plays it.

Channel W comes first in both common orders, so a `.cfg` that names channel
0 plays it as a mono reverb. Save this as `hall.cfg` beside the B-format
`hall.wav`, with the first number changed to the file's sample rate:

```text
// sample rate, input channels, output channels, speaker mask
96000 1 1 0
// input delay in milliseconds
0
// output delay in milliseconds
0
// the B-format file's W channel as a mono reverb
hall.wav
0
0
0
```

It works on mono, stereo or wider tracks
([mono and true-stereo IR files](configuration.md#mono-and-true-stereo-ir-files)). For
files that hold one channel each, such as C4DM's, use the W or
omnidirectional file directly instead.

## Check that it worked

Render a short piece with the command-line tool:

```sh
convolver --filter hall.cfg --input dry.wav --output wet.wav --gain-db auto
```

The tool prints how it routes the filter, such as `the filter has one input
and one output, so it filters each of the 2 input channels on its own`.
Listen to `wet.wav`: you should hear the space around the sound. In a
plug-in, choose the file or `.cfg` and read the Status and Routing line
([Convolver's window](plugins.md#convolvers-window)). If the filter is
refused, the message gives the reason; the usual one is a different sample
rate ([troubleshooting](troubleshooting.md)).
