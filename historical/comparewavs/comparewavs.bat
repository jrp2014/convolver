
set comparewavs=Debug\comparewavs.exe

%comparewavs% ..\Media\drcsweep-shortselection.wav 0 ..\Media\Trace1-1.wav 2 ..\Media\Diff1-1.wav >Results\Trace1-1.csv
%comparewavs% ..\Media\drcsweep-shortselection.wav 0 ..\Media\Trace1-2.wav 4 ..\Media\Diff1-2.wav >Results\Trace1-2.csv
%comparewavs% ..\Media\drcsweep-shortselection.wav 0 ..\Media\Trace1-3.wav 4 ..\Media\Diff1-3.wav >Results\Trace1-3.csv
%comparewavs% ..\Media\drcsweep-shortselection.wav 0 ..\Media\Trace1-4.wav 8 ..\Media\Diff1-4.wav >Results\Trace1-4.csv