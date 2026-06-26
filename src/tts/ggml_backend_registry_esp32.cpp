#include <ggml-backend.h>
#include <ggml-cpu.h>
#include <ggml-backend-impl.h>
#include <ggml-impl.h>

#include <cctype>
#include <cstring>

namespace {

constexpr size_t kExtraRegCapacity = 4;
constexpr size_t kExtraDeviceCapacity = 4;

ggml_backend_reg_t extraRegs[kExtraRegCapacity] = {};
size_t extraRegCount = 0;
ggml_backend_dev_t extraDevices[kExtraDeviceCapacity] = {};
size_t extraDeviceCount = 0;

bool striequals(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  while (*a != '\0' && *b != '\0') {
    if (std::tolower(static_cast<unsigned char>(*a)) !=
        std::tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

size_t cpuDeviceCount() { return ggml_backend_reg_dev_count(ggml_backend_cpu_reg()); }

ggml_backend_dev_t cpuDevice(size_t index = 0) {
  return ggml_backend_reg_dev_get(ggml_backend_cpu_reg(), index);
}

} // namespace

void ggml_backend_register(ggml_backend_reg_t reg) {
  if (reg == nullptr || reg == ggml_backend_cpu_reg()) {
    return;
  }
  for (size_t i = 0; i < extraRegCount; i++) {
    if (extraRegs[i] == reg) {
      return;
    }
  }
  if (extraRegCount < kExtraRegCapacity) {
    extraRegs[extraRegCount++] = reg;
  }
}

void ggml_backend_device_register(ggml_backend_dev_t device) {
  if (device == nullptr) {
    return;
  }
  for (size_t i = 0; i < extraDeviceCount; i++) {
    if (extraDevices[i] == device) {
      return;
    }
  }
  if (extraDeviceCount < kExtraDeviceCapacity) {
    extraDevices[extraDeviceCount++] = device;
  }
}

size_t ggml_backend_reg_count() { return 1 + extraRegCount; }

ggml_backend_reg_t ggml_backend_reg_get(size_t index) {
  GGML_ASSERT(index < ggml_backend_reg_count());
  if (index == 0) {
    return ggml_backend_cpu_reg();
  }
  return extraRegs[index - 1];
}

ggml_backend_reg_t ggml_backend_reg_by_name(const char *name) {
  for (size_t i = 0; i < ggml_backend_reg_count(); i++) {
    ggml_backend_reg_t reg = ggml_backend_reg_get(i);
    if (striequals(ggml_backend_reg_name(reg), name)) {
      return reg;
    }
  }
  return nullptr;
}

size_t ggml_backend_dev_count() { return cpuDeviceCount() + extraDeviceCount; }

ggml_backend_dev_t ggml_backend_dev_get(size_t index) {
  GGML_ASSERT(index < ggml_backend_dev_count());
  const size_t cpuCount = cpuDeviceCount();
  if (index < cpuCount) {
    return cpuDevice(index);
  }
  return extraDevices[index - cpuCount];
}

ggml_backend_dev_t ggml_backend_dev_by_name(const char *name) {
  for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
    ggml_backend_dev_t dev = ggml_backend_dev_get(i);
    if (striequals(ggml_backend_dev_name(dev), name)) {
      return dev;
    }
  }
  return nullptr;
}

ggml_backend_dev_t ggml_backend_dev_by_type(enum ggml_backend_dev_type type) {
  for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
    ggml_backend_dev_t dev = ggml_backend_dev_get(i);
    if (ggml_backend_dev_type(dev) == type) {
      return dev;
    }
  }
  return nullptr;
}

ggml_backend_t ggml_backend_init_by_name(const char *name,
                                         const char *params) {
  ggml_backend_dev_t dev = ggml_backend_dev_by_name(name);
  return dev != nullptr ? ggml_backend_dev_init(dev, params) : nullptr;
}

ggml_backend_t ggml_backend_init_by_type(enum ggml_backend_dev_type type,
                                         const char *params) {
  ggml_backend_dev_t dev = ggml_backend_dev_by_type(type);
  return dev != nullptr ? ggml_backend_dev_init(dev, params) : nullptr;
}

ggml_backend_t ggml_backend_init_best(void) {
  return ggml_backend_init_by_type(GGML_BACKEND_DEVICE_TYPE_CPU, nullptr);
}

ggml_backend_reg_t ggml_backend_load(const char *) { return nullptr; }

void ggml_backend_unload(ggml_backend_reg_t) {}

void ggml_backend_load_all(void) {}

void ggml_backend_load_all_from_path(const char *) {}
