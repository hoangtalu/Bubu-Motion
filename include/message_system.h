#pragma once
#include <Arduino.h>

namespace MessageSystem {
  using FinishedCallback = void (*)();

  void begin();
  void open(FinishedCallback onFinished);
  void close();
  bool isOpen();
}
