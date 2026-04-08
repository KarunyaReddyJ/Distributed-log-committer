#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "../definitions.h"

namespace utils {
    void write_int16(bytestream& stream, int16_t value);
    void write_int32(bytestream& stream, int32_t value);
    void write_string(bytestream& stream,  std::string& value);
}
