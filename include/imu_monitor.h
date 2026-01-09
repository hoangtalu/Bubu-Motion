#pragma once

#include <stdint.h>

namespace ImuMonitor {

void begin();
void update(uint32_t nowMs);

}  // namespace ImuMonitor
