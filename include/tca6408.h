#ifndef TCA6408_H
#define TCA6408_H

#include <stdint.h>

namespace TCA6408 {

// Initialize TCA6408 (configure all pins as inputs)
// Returns true if successful
bool begin();

// Read the input port register
// Returns true if successful, value contains the 8-bit input state
bool readInputs(uint8_t& value);

// Read the configuration register
// Returns true if successful, value contains the 8-bit config
bool readConfig(uint8_t& value);

}  // namespace TCA6408

#endif  // TCA6408_H