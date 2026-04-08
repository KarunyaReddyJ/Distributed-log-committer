#include "serializer.h"
#include <stdexcept>
void utils::write_int16(bytestream &buffer, int16_t val){
    uint8_t high_byte = (val >> 8) & 0xFF;
    uint8_t low_byte = (val) & 0xFF;

    buffer.push_back(high_byte);
    buffer.push_back(low_byte);
}

void utils::write_int32(bytestream &buffer, int32_t val){
    uint32_t uval = static_cast<uint32_t>(val);
    uint16_t high_dbyte = (uval >> 16) & 0xFFFF;
    utils::write_int16(buffer, high_dbyte);
    uint16_t low_dbyte = (uval) & 0xFFFF;
    utils::write_int16(buffer, low_dbyte);
}

void utils::write_string(bytestream &buffer, std::string &val){
    if (val.size() > MAX_STRING_SIZE){
        throw std::runtime_error("String too long");
    }

    int16_t length = static_cast<int16_t>(val.size());

    // Write length in big-endian
    utils::write_int16(buffer, length);

    // Write characters
    for (uint8_t ch : val){
        buffer.push_back(ch);
    }
}
// #include<iostream>

// int main(){
//     int16_t i16;
//     int32_t i32;
//     std::string str;
//     std::cin>>i16>>i32>>str;
//     bytestream buffer;
//     utils::write_int16(buffer,i16);
//     utils::write_int32(buffer,i32);
//     utils::write_string(buffer,str);
//     for(uint8_t& byte : buffer){
//         printf("%02X",byte);
//     }
// }