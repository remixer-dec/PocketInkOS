#include "ui/components/file_picker_component.h"

#include "apps/files_app.h"
#include "sys/global.h"
#include "sys/psram_allocator.h"

#include <cstring>
#include <new>

namespace {

bool copyText(char *out, size_t outSize, const char *text) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  const char *source = text != nullptr ? text : "";
  const size_t length = std::strlen(source);
  if (length >= outSize) {
    return false;
  }
  std::memcpy(out, source, length + 1U);
  return true;
}

void drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y,
                  uint8_t size = 1) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.setTextSize(size);
  gfx.getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
  gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(width)) / 2 - x1, y);
  gfx.print(text);
}

} // namespace

bool FilePickerComponent::request(const char *nextInitialPath,
                                  const char *nextExtensionFilter) {
  release();
  if (!copyText(initialPath, sizeof(initialPath), nextInitialPath) ||
      !copyText(extensionFilter, sizeof(extensionFilter),
                nextExtensionFilter)) {
    state = Result::Error;
    return false;
  }
  state = Result::Opening;
  return true;
}

void FilePickerComponent::draw(Adafruit_GFX &gfx) const {
  // The picker is a full-screen modal. Clear the app underneath before drawing
  // so stale app content and runtime errors cannot show through.
  gfx.fillScreen(0);
  if (state == Result::Opening) {
    gfx.setTextColor(1);
    drawCentered(gfx, "FILES", 72, 2);
    drawCentered(gfx, "LOADING", 104);
  } else if (browser != nullptr) {
    browser->draw(gfx);
  }
}

bool FilePickerComponent::update() {
  if (state == Result::Opening) {
    // request() runs on the caller's task; construction is intentionally
    // deferred until this method is driven by the shell/main core.
    void *memory = psram::allocate(sizeof(FilesApp));
    if (memory == nullptr) {
      state = Result::Error;
      return true;
    }
    browser = new (memory) FilesApp();
    if (!browser->beginPicker(initialPath, extensionFilter)) {
      release();
      state = Result::Error;
      return true;
    }
    state = Result::Active;
    return true;
  }
  if (state != Result::Active || browser == nullptr) {
    return false;
  }
  const bool dirty = browser->update();
  if (browser->pickerCancelled()) {
    state = Result::Cancelled;
  } else if (browser->pickerSelectionReady()) {
    state = Result::Selected;
  }
  return dirty;
}

// Consume input while Opening so the launch gesture cannot leak into the
// newly constructed browser.
bool FilePickerComponent::handleTouch(const TouchPoint &point) {
  if (state == Result::Opening) {
    return true;
  }
  if (state != Result::Active || browser == nullptr) {
    return false;
  }
  const bool handled = browser->handleTouch(point);
  if (browser->pickerCancelled()) {
    state = Result::Cancelled;
  } else if (browser->pickerSelectionReady()) {
    state = Result::Selected;
  }
  return handled;
}

bool FilePickerComponent::handleMenuButton() {
  if (state == Result::Opening) {
    return true;
  }
  if (state != Result::Active || browser == nullptr) {
    return false;
  }
  if (!browser->handleMenuButton()) {
    cancel();
  }
  return true;
}

bool FilePickerComponent::handlePowerButton() {
  return state == Result::Opening ||
         (state == Result::Active && browser != nullptr &&
          browser->handlePowerButton());
}

void FilePickerComponent::cancel() {
  if (state == Result::Opening) {
    state = Result::Cancelled;
  } else if (state == Result::Active && browser != nullptr) {
    browser->cancelPicker();
    state = Result::Cancelled;
  }
}

FilePickerComponent::Result FilePickerComponent::result() const {
  return state;
}

bool FilePickerComponent::overlayActive() const {
  return state == Result::Opening || state == Result::Active;
}

bool FilePickerComponent::takePath(char *out, size_t outSize) {
  return state == Result::Selected && browser != nullptr &&
         browser->takePickedPath(out, outSize);
}

void FilePickerComponent::release() {
  if (browser != nullptr) {
    browser->release();
    browser->~FilesApp();
    psram::release(browser);
    browser = nullptr;
  }
  state = Result::Idle;
  initialPath[0] = '/';
  initialPath[1] = '\0';
  extensionFilter[0] = '\0';
}
