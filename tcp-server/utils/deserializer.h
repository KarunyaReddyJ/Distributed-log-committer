#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "../definitions.h"

namespace utils {
    int16_t read_int16(const bytestream&, int32_t& );
    int32_t read_int32(const bytestream&, int32_t& );
    std::string read_string(const bytestream&,int32_t& );
}
