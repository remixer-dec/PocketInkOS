#include "contact_links_app.h"
#include "secrets_config.h"
#include "ui_helpers.h"

#include <Arduino.h>
#include <cstring>

const ContactLinksApp::ContactLink ContactLinksApp::LINKS[LINK_COUNT] = {
    {"TG", "Telegram", SECRET_CONTACT_TELEGRAM_URL},
    {"@", "E-Mail", SECRET_CONTACT_EMAIL_URL},
    {"PH", "Phone", SECRET_CONTACT_PHONE_URL},
    {"in", "Linked-in", SECRET_CONTACT_LINKEDIN_URL},
    {"WA", "WhatsApp", SECRET_CONTACT_WHATSAPP_URL},
};

static const char *CONTACT_NAME = SECRET_CONTACT_NAME;
static const char *CONTACT_PROFESSION = SECRET_CONTACT_PROFESSION;

void ContactLinksApp::reset() {
  showingQr = false;
  qr.reset();
}

bool ContactLinksApp::hasActiveSession() const { return showingQr; }

void ContactLinksApp::draw(Adafruit_GFX &gfx) {
  if (showingQr) {
    qr.draw(gfx);
    return;
  }
  drawMenu(gfx);
}

bool ContactLinksApp::handleTouch(const TouchPoint &point) {
  if (showingQr) {
    showingQr = false;
    qr.reset();
    return true;
  }

  if (point.x < 20 || point.x >= 200 || point.y < 48 || point.y >= 152) {
    return false;
  }

  int col = (point.x - 20) / 60;
  int row = (point.y - 48) / 52;
  int index = row * 3 + col;
  if (col < 0 || col > 2 || row < 0 || row > 1 || index >= LINK_COUNT) {
    return false;
  }

  openLink(index);
  return true;
}

bool ContactLinksApp::handlePowerButton() {
  if (!showingQr) {
    return false;
  }
  showingQr = false;
  qr.reset();
  return true;
}

void ContactLinksApp::openLink(int index) {
  if (index < 0 || index >= LINK_COUNT) {
    return;
  }
  qr.reset();
  qr.setText(LINKS[index].url);
  showingQr = true;
}

void ContactLinksApp::drawMenu(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  gfx.getTextBounds("LINKS", 0, 0, &titleX, &titleY, &titleW, &titleH);
  gfx.setCursor((200 - static_cast<int>(titleW)) / 2 - titleX, 14);
  gfx.print("LINKS");

  for (int i = 0; i < LINK_COUNT; i++) {
    int row = i / 3;
    int col = i % 3;
    int x = 20 + col * 60;
    int y = 48 + row * 52;

    gfx.drawRect(x, y, 40, 32, 1);
    gfx.setTextSize(strlen(LINKS[i].icon) > 1 ? 1 : 2);
    int16_t iconX;
    int16_t iconY;
    uint16_t iconW;
    uint16_t iconH;
    gfx.getTextBounds(LINKS[i].icon, 0, 0, &iconX, &iconY, &iconW, &iconH);
    gfx.setCursor(x + (40 - static_cast<int>(iconW)) / 2 - iconX,
                  y + (32 - static_cast<int>(iconH)) / 2 - iconY);
    gfx.print(LINKS[i].icon);

    gfx.setTextSize(1);
    int16_t labelX;
    int16_t labelY;
    uint16_t labelW;
    uint16_t labelH;
    gfx.getTextBounds(LINKS[i].label, 0, 0, &labelX, &labelY, &labelW,
                      &labelH);
    gfx.setCursor(x + (40 - static_cast<int>(labelW)) / 2 - labelX, y + 35);
    gfx.print(LINKS[i].label);
  }

  gfx.setTextSize(1);
  int16_t nameX;
  int16_t nameY;
  uint16_t nameW;
  uint16_t nameH;
  gfx.getTextBounds(CONTACT_NAME, 0, 0, &nameX, &nameY, &nameW, &nameH);
  gfx.setCursor((200 - static_cast<int>(nameW)) / 2 - nameX, 166);
  gfx.print(CONTACT_NAME);

  int16_t professionX;
  int16_t professionY;
  uint16_t professionW;
  uint16_t professionH;
  gfx.getTextBounds(CONTACT_PROFESSION, 0, 0, &professionX, &professionY,
                    &professionW, &professionH);
  gfx.setCursor((200 - static_cast<int>(professionW)) / 2 - professionX, 180);
  gfx.print(CONTACT_PROFESSION);

  gfx.setTextSize(1);
}
