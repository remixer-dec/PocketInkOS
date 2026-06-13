#ifndef SHELL_BUTTONS_H
#define SHELL_BUTTONS_H

typedef void (*ShellButtonCallback)();

struct ShellButtonHandlers {
  ShellButtonCallback onMenu = nullptr;
  ShellButtonCallback onMenuDouble = nullptr;
  ShellButtonCallback onMenuLong = nullptr;
  ShellButtonCallback onPower = nullptr;
  ShellButtonCallback onPowerDouble = nullptr;
  ShellButtonCallback onPowerLong = nullptr;
};

void shellButtonsBegin(const ShellButtonHandlers &handlers);
void shellButtonsDispatch();

#endif
