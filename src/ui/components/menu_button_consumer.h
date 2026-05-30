#ifndef MENU_BUTTON_CONSUMER_H
#define MENU_BUTTON_CONSUMER_H

class MenuButtonConsumer {
public:
  virtual bool handleMenuButton() = 0;
  virtual bool handleMenuDoubleButton() = 0;
  virtual bool handleMenuLongButton() = 0;
};

void setActiveMenuButtonConsumer(MenuButtonConsumer *consumer);
void clearActiveMenuButtonConsumer(MenuButtonConsumer *consumer);
bool handleActiveMenuButton();
bool handleActiveMenuDoubleButton();
bool handleActiveMenuLongButton();

#endif
