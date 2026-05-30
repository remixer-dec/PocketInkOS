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
  bool update();
  bool handlePowerButton();
  bool hasActiveSession() const;

private:
  struct ContactLink {
    const char *icon;
    const char *label;
    const char *url;
  };

  static const int LINK_COUNT = 6;
  static const ContactLink LINKS[LINK_COUNT];

  bool showingQr = false;
  bool showingLinkText = false;
  int activeLinkIndex = -1;
  int pressedLinkIndex = -1;
  int pendingLinkIndex = -1;
  unsigned long pressedAt = 0;
  QrApp qr;

  void openLink(int index);
  void drawMenu(Adafruit_GFX &gfx);
  void drawLinkText(Adafruit_GFX &gfx);
};

#endif
