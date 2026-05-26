#ifndef CONTACT_LINKS_APP_H
#define CONTACT_LINKS_APP_H

#include "qr_app.h"
#include "touch_input.h"
#include <Adafruit_GFX.h>

class ContactLinksApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool handlePowerButton();
  bool hasActiveSession() const;

private:
  struct ContactLink {
    const char *icon;
    const char *label;
    const char *url;
  };

  static const int LINK_COUNT = 5;
  static const ContactLink LINKS[LINK_COUNT];

  bool showingQr = false;
  QrApp qr;

  void openLink(int index);
  void drawMenu(Adafruit_GFX &gfx);
};

#endif
