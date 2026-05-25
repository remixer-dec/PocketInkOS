#include "menu_button_consumer.h"

static MenuButtonConsumer *activeConsumer = nullptr;

void setActiveMenuButtonConsumer(MenuButtonConsumer *consumer) {
  activeConsumer = consumer;
}

void clearActiveMenuButtonConsumer(MenuButtonConsumer *consumer) {
  if (activeConsumer == consumer) {
    activeConsumer = nullptr;
  }
}

bool handleActiveMenuButton() {
  return activeConsumer != nullptr && activeConsumer->handleMenuButton();
}

bool handleActiveMenuDoubleButton() {
  return activeConsumer != nullptr && activeConsumer->handleMenuDoubleButton();
}

bool handleActiveMenuLongButton() {
  return activeConsumer != nullptr && activeConsumer->handleMenuLongButton();
}
