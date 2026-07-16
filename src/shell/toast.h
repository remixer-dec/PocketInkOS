/// Reusable transient notification overlay owned by the shell.
#ifndef SHELL_TOAST_H
#define SHELL_TOAST_H

#include <stdint.h>

class AppDisplay;

/// Shows or replaces the current toast. Empty text clears it immediately.
void showShellToast(const char *message, uint32_t durationMs = 1800U);

/// Advances expiry state and reports whether the shell must redraw.
bool updateShellToast();

/// Draws the current toast over an already-rendered shell frame.
void drawShellToast(AppDisplay &display);

#endif
