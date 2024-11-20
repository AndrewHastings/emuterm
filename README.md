# emuterm

**emuterm** emulates an old terminal by handling its output control
sequences. The intended use is to run old programs on simulated hardware
(e.g., the SIMH HP2000 TSB) that assume a particular type of serial terminal.

**emuterm** is a command-line program designed to run inside a terminal
emulator that recognizes ANSI and xterm control sequences, e.g. **xterm**
or **gnome-terminal**. It is NOT a standalone GUI program.

## Emulation

Currently supported terminals:

- Digilog 33 (for running the HP2000 TSB **PLOT33** program)

- No emulation (pass-through; for access to the "Features" below)

## Features

- Emulate the output baud rates of old terminals (e.g., 300 baud or 30
characters per second).

To overcome the inability of X Windows to copy and paste non-printing
characters:

- Selectively capture raw terminal output (including non-printing
characters) to a file.

- Transmit the contents of a file (including non-printing characters) as
terminal input.

## References

- HP2000 TSB simulator: https://simh.trailing-edge.com/hp/

- Digilog terminal documentation: https://bitsavers.org/pdf/digi-log/

- Terminals wiki: https://terminals-wiki.org

- Termcap/Terminfo Resources Page: http://www.catb.org/~esr/terminfo/

- Xterm control sequences: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
