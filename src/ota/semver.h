#pragma once
#include <Arduino.h>

inline int semverCompare(const String& a, const String& b) {
  int a1=0,a2=0,a3=0, b1=0,b2=0,b3=0;
  sscanf(a.c_str(), "%d.%d.%d", &a1,&a2,&a3);
  sscanf(b.c_str(), "%d.%d.%d", &b1,&b2,&b3);

  if (a1 != b1) return a1 - b1;
  if (a2 != b2) return a2 - b2;
  return a3 - b3;
}