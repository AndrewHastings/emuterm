# emuterm

**emuterm** emulates an old serial terminal by handling its output control
sequences. The intended use is to run old programs (e.g., **PLOT33** from
the HP2000 TSB system) which assume a particular type of serial terminal
(e.g., the Digi-Log 33) in simulation (e.g., the SIMH HP2000 TSB)

**emuterm** is a command-line program designed to run inside a terminal
emulator that handles ANSI and xterm control sequences, e.g. **xterm**
or **gnome-terminal**. It is NOT a standalone GUI program.

**emuterm** can also act as a pass-through (no emulation) for access to
the "Features" below (e.g., to run the BSD **rain** program at a rate
that more closely matches its original design, 9600 baud or less).

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

- The output parsing algorithm was not designed to handle control
sequences with optional parameters as is common in ANSI control
sequences. Therefore, terminals with ANSI-style control sequences (e.g.,
DEC VT100, **xterm**) are not supported. (Modern terminal emulators such
as **gnome-terminal** usually handle a large set of ANSI-style control
sequences.)

- In general, function keys are not supported on input. However,
**emuterm** will try to translate arrow keys as defined by the termcap
"ku", "kd", "kr", and "kl" capabilities.

Caveats:

- For best results, resize the ANSI terminal emulator in which **emuterm**
is running to match the number of columns of the old serial terminal.
On startup, **emuterm** sends an ANSI control sequence to set the right
margin, but many ANSI terminal emulators do not recognize it. (Most old
serial terminals are 80 columns.)

- The modern **vim** and **ncurses** have some limitations in supporting
old serial terminals that the original BSD **vi** and **curses** did *not*
have. In particular, **vim** requires the termcap "cm" capability while
BSD **vi** did not. Try running a "cdc713" terminal type with the
"Traditional Vi" (see "References" below) versus running with **vim**.

- The modern **terminfo** database defines some terminal capabilities
differently than the BSD **termcap** file. As a result, **emuterm** may
ignore or mishandle some control sequences sent by **vim** when run
against old terminals. To avoid this problem, follow the instructions in
the included **termcap** file to add the **termcap** version of the
terminal's capabilities to the **terminfo** database.

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

- Digi-Log terminal documentation: https://bitsavers.org/pdf/digi-log/

- Control Data 713 terminal documentation: https://bitsavers.org/pdf/cdc/terminal/713/

- Tset command: https://man.openbsd.org/OpenBSD-2.2/tset.1

- Termcap file format: https://man.openbsd.org/OpenBSD-2.2/termcap.5

- Termcap/Terminfo Resources Page: http://www.catb.org/~esr/terminfo/

- HP2000 TSB simulator: https://simh.trailing-edge.com/hp/

- 4.2BSD Vax 11/780 simulator: https://sourceforge.net/projects/bsd42/

- The Traditional Vi: https://ex-vi.sourceforge.net/

- Xterm control sequences: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
