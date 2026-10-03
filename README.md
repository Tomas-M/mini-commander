# mini-commander
Mini Commander is very simplified clone of Midnight Commander for Linux.
It even includes viewer and editor.

I would like to call myself an author, but that's not so easy.

The majority of the code, including this text itself, was initially written by
ChatGPT 4. Newer bugfixes were automatically detected and fixed by GPT-6 Astra.

ChatGPT doesn't generate code with an explicit license attached to it. The
code it provides in responses is intended for educational and informational
purposes, and users are free to use it as they see fit. So, the license
of Mini Commander is GNU GPL v3.

![mc](https://github.com/Tomas-M/mini-commander/assets/2259370/3ec02529-7e8d-4d74-9468-01f35b705b0f)

Tomas M
slax.org

## Features

- **Built-in viewer and editor:** open files with `F3` or edit them with `F4`,
  without launching an external program. Save in the editor with `F2`.
- **UTF-8 support:** view and edit multilingual text, including wide characters
  and combining accents, with correct cursor movement and deletion. Dialog text
  fields accept UTF-8, and panels display international file and directory names.
- **Syntax highlighting:** basic highlighting for C source and headers (`.c`,
  `.h`) and shell scripts (`.sh`). Files starting with `#!/` also receive shell
  highlighting, even without an extension.
- **See invisible characters:** tabs have visible markers, and the editor marks
  control characters. Its status line shows the line, column, byte position,
  file size, and Unicode code point under the cursor.
- **Editor save protection:** prompts before closing unsaved changes, preserves
  file ownership and permissions, and edits through symbolic links while keeping
  the links intact.
- **Two independent panels:** switch with `Tab`. Each panel has its own sorting
  options (`F2`): name, size, or modification time, in either direction, with
  directories first or mixed with files.
- **Batch file operations:** mark items with `Insert`, then copy (`F5`),
  move/rename within the same filesystem (`F6`), or delete (`F8`). Copying and
  deleting directories includes their contents.
- **Create nested directories:** `F7` accepts a path such as `projects/demo/src`
  and creates missing parent directories too.
- **Copy progress and controls:** see progress for the current file and the
  whole operation, skip a file, or abort. Error dialogs offer retry and skip
  options; overwrite prompts can apply a choice to the remaining files.
- **Symbolic links and file colors:** link targets are displayed and broken
  links are marked. Executables, selected archive formats, and source files
  have distinct colors. Copying a symbolic link copies the link itself.
- **Mouse navigation:** select items, double-click to open directories or run
  executables, and scroll with the wheel in supported terminals.
- **Monochrome mode:** run `./mc -b` or `./mc --nocolor` to disable colors.

### Less obvious shortcuts

| Shortcut | Action |
| --- | --- |
| `Alt+S`, then type | Jump to a filename by its prefix; press `Alt+S` again while searching for the next match. |
| `Ctrl+Space` | Calculate the size of the directory under the cursor, including its contents (with no other items selected). |
| `Alt+Enter` | Insert the filename under the cursor into the command line. |
| `Alt+A` | Insert the active panel's directory path into the command line. |
| `Shift+F5` / `Shift+F6` / `Shift+F7` | Copy, move/rename, or create a directory with the name under the cursor prefilled, without its path. Typing appends to the name; relative targets use the active directory. |
| `Enter` | Execute a typed shell command in the active directory; with an empty command line, open a directory or run an executable. |
| `Ctrl+O` | Temporarily reveal terminal output; press any key to return to the panels. |
| `Ctrl+R` or `F9` | Refresh both panels. |
| `Ctrl+L` | Redraw the screen. |


Usage:

    # Debian/Ubuntu build dependencies:
    sudo apt-get install build-essential libncursesw5-dev

    make
    ./mc

    # Result of compilation is standalone 'mc' binary, it does not need anything else.
    # There is no make install because 'mc' would interfere with midnight commander.
    # So install it manually, for example copy ./mc to your path if you like

Use a UTF-8 terminal and locale to display international file and directory names
correctly, for example `LC_ALL=C.UTF-8 ./mc`. Both builds use Unicode-capable
ncurses (`ncursesw`), and panel labels are shortened without splitting UTF-8
characters. The viewer, editor, and dialog text fields support UTF-8, including
wide characters and combining accents. Editor search ignores letter case using
the active locale. Files keep their original bytes unless edited; other text
encodings are not converted automatically. Complex joined emoji sequences are
not treated as a single character. The main command line and panel quick search
still use byte-oriented input.

For a small, fully static 32-bit x86 binary built with musl and ncurses, run
`sh static/build.sh`. The script downloads its dependencies, builds and packs
`static/mc`, and cleans up afterwards. See [static build instructions](static/README.md).
