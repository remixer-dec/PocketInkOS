#ifndef SYSTEM_STAGE_SERVICE_H
#define SYSTEM_STAGE_SERVICE_H

/// Initializes level-2 system services shared by regular and periodic boots.
/// Audio GPIOs become available in a powered-off state; playback activates them.
class SystemStageService {
public:
  void initialize();

private:
  bool started = false;
};

#endif
