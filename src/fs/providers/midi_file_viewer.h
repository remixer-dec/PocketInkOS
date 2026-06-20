#ifndef MIDI_FILE_VIEWER_H
#define MIDI_FILE_VIEWER_H

#include "fs/file_viewer_registry.h"

extern const FileViewerExtension MIDI_FILE_VIEWER;

uint32_t midiViewerDurationMs(const char *providerId, const char *path);
bool midiViewerTogglePlayback(FileViewerRuntime &runtime);
void midiViewerClose();

#endif
