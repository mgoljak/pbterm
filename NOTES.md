# pbterm (PocketBook Verse PB629) - working notes

Build: `SDK=<SDK65>/SDK-B288 ./build.sh` (SDK = branch 6.5 of github.com/pocketbook/SDK_6.3.0;
toolchain needs LD_LIBRARY_PATH=$SDK/usr/lib, build.sh sets it). Output: build/pbterm.app.

Device facts: no pty (posix_openpt -> EACCES), shell runs on pipes. Log:
/mnt/ext1/system/share/pbterm/pbterm.log. System panel must be off (SetPanelType) or
the framebuffer is offset. Config (optional): /mnt/ext1/system/config/pbterm.cfg.

Files (src/):
- Vt.cpp/hpp         VT100/xterm emulator, cell grid, scrollback (host-testable, vt_test.cpp)
- ExtraKeys.cpp/hpp  key rows: extra keys + on-screen keyboard, sticky Ctrl/Alt/Shift (xk_test.cpp)
- Display.cpp/hpp    draws grid, key rows, selection; geometry, partial/full e-ink updates
- Term.cpp/hpp       shell process; pipe-mode line editor (echo, history, TAB, ^C) in Term::input()
- Messenger.cpp      message hub (template specialisations per message type, see Message.hpp)
- Pointer_Handler    taps/swipes: key rows, selection, scroll; Menu_Handler: long-press menu
- Keyboard_Handler   system "Enter shell command" box (only when onscreen_keyboard : 0)

Known limits: full-screen programs (vi) get keys only after ENTER (no tty).
