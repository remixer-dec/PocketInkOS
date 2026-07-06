#ifndef MENU_BUTTON_CONSUMER_H
#define MENU_BUTTON_CONSUMER_H

/// Interface for active screens that consume menu-button events.
class MenuButtonConsumer {
public:
  /// Handles a single menu-button press.
  virtual bool handleMenuButton() = 0;
  /// Handles a double menu-button press.
  virtual bool handleMenuDoubleButton() = 0;
  /// Handles a long menu-button press.
  virtual bool handleMenuLongButton() = 0;
};

/// Sets the active menu-button consumer.
void setActiveMenuButtonConsumer(MenuButtonConsumer *consumer);
/// Clears the active consumer when it matches the supplied pointer.
void clearActiveMenuButtonConsumer(MenuButtonConsumer *consumer);
/// Dispatches a single menu-button press to the active consumer.
bool handleActiveMenuButton();
/// Dispatches a double menu-button press to the active consumer.
bool handleActiveMenuDoubleButton();
/// Dispatches a long menu-button press to the active consumer.
bool handleActiveMenuLongButton();

#endif
