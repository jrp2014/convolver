# Preview troubleshooting

Use the archive matching your CPU and OS. A deployment-target flag or a successful
build alone does not establish compatibility with an older OS. No production
compatibility floor is advertised yet.

If a host cannot scan the plug-in, check its diagnostic log for missing libraries,
architecture mismatch or signature rejection. Follow the host's documented rescan
procedure after correcting the cause. Do not disable operating-system security.

A preset must match both the host sample rate and mono/stereo layout. Change the
host configuration or import the matching preset; Convolver does not resample.
Malformed or mismatched state is rejected and the last valid state is preserved.
A converter failure prints the cause to stderr and does not replace an existing
file. Use a new output filename. Keep levels low when trying unfamiliar IRs.

Report the package hash, OS/build, CPU, host version, sample rate, channel layout,
buffer size, error log and a synthetic minimal reproduction. Do not include private
audio or session files. The synthetic examples provide safe starting inputs.
