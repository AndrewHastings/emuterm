# emuterm

**emuterm** emulates an old serial terminal by handling its output control
sequences. The intended use is to run old programs (e.g., **PLOT33** from
the HP2000 TSB system) which assume a particular type of serial terminal
(e.g., the Digi-Log 33) in simulation (e.g., the SIMH HP2000 TSB)

**emuterm** is a command-line program designed to run inside a terminal
emulator that handles ANSI and xterm control sequences, e.g. **xterm**
or **gnome-terminal**. It is NOT a standalone GUI program.

**emuterm** can also act as a pass-through (no emulation) for access to
the "Features" below (e.g., to run the BSD **rain** program at 1200 baud).

## Emulation

**emuterm** consults the old **termcap** text database to extract the
control sequences of the emulated terminal. (Both the modern **terminfo**
and the old **termcap** are designed for *generating* control sequences,
not *recognizing* them. Because **termcap** string capabilities are
simpler, they are easier to recognize and parse on output.)

Since modern distros have dropped the **termcap** file, and old terminals
are usually omitted from the default **terminfo** database install,
a **termcap** file is included with **emuterm**. It should be copied to
`$HOME/.local/share/misc/termcap`.

Limitations:

- Supports a minimal set of terminal capabilities, mostly just those
typically needed by old **vi** implementations. **emuterm** detects some
unsupported capabilities at startup, but others may be ignored or
partially passed through at runtime.

- Handles only output control sequences, not input control sequences
(e.g., as sent by arrow and function keys).

## Additional Features

- Since modern implementations of the **tset** program are no longer
designed to set the TERMCAP environment variable, a new program **tsete**
is included to provide that functionality.

- **emuterm** can emulate the output baud rates of old terminals (e.g.,
300 baud or 30 characters per second).

To overcome the inability of X Windows to copy and paste non-printing
characters, **emuterm** can:

- Selectively capture raw terminal output (including non-printing
characters) to a file.

- Transmit the contents of a file (including non-printing characters) as
terminal input.

## References

- HP2000 TSB simulator: https://simh.trailing-edge.com/hp/

- Digi-Log terminal documentation: https://bitsavers.org/pdf/digi-log/

- Tset command: https://man.openbsd.org/OpenBSD-2.2/tset.1

- Termcap file format: https://man.openbsd.org/OpenBSD-2.2/termcap.5

- Termcap/Terminfo Resources Page: http://www.catb.org/~esr/terminfo/

- Xterm control sequences: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
