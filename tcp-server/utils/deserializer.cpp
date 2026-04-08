
#include "deserializer.h"
#include <stdexcept>

void check_overflow(const bytestream& stream,int32_t& offset,int32_t data_size){
    if(stream.size()<offset+data_size){
        throw std::runtime_error("Stream overflow");
    }
}

int16_t utils::read_int16(const bytestream& stream,int32_t& offset){
    check_overflow(stream,offset,2);
    uint8_t high_byte = stream[offset++];
    uint8_t low_byte = stream[offset++];
    
    int16_t value = (static_cast<int16_t>(high_byte) << 8) |static_cast<int16_t>(low_byte);
    return value;
}


int32_t utils::read_int32(const bytestream& stream,int32_t& offset){
    check_overflow(stream,offset,4);
    int16_t high_dbyte = utils::read_int16(stream,offset);
    int16_t low_dbyte = utils::read_int16(stream,offset);
    int32_t num = (static_cast<int32_t>(high_dbyte) << 16) |(static_cast<uint16_t>(low_dbyte));
    return num;
}

std::string utils::read_string(const bytestream& stream,int32_t& offset){
    int16_t string_length = utils::read_int16(stream,offset);
    if(string_length > MAX_STRING_SIZE){
        throw std::runtime_error("String too long");
    }
    check_overflow(stream,offset,string_length);
    std::string value;
    for (int i = 0; i < string_length; i++) {
        value.push_back(stream[offset++]);
    }
    return value;
}

// #include <iostream>
// #include <vector>
// #include <iomanip>
// #include "deserializer.h"

// using bytestream = std::vector<uint8_t>;

// // helper: convert hex string → byte stream
// bytestream hex_to_bytes(const std::string& hex) {
//     bytestream bytes;

//     for (size_t i = 0; i < hex.length(); i += 2) {
//         std::string byte_str = hex.substr(i, 2);
//         uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
//         bytes.push_back(byte);
//     }

//     return bytes;
// }

// int main() {
//     std::string hex_input = "00020000000C000568656C6C6F";

//     bytestream stream = hex_to_bytes(hex_input);

//     int32_t offset = 0;

//     try {
//         int16_t val16 = utils::read_int16(stream, offset);
//         int32_t val32 = utils::read_int32(stream, offset);
//         std::string str = utils::read_string(stream, offset);

//         std::cout << "Parsed values:\n";
//         std::cout << "int16: " << val16 << "\n";
//         std::cout << "int32: " << val32 << "\n";
//         std::cout << "string: " << str << "\n";

//     } catch (const std::exception& e) {
//         std::cerr << "Error: " << e.what() << "\n";
//     }

//     return 0;
// }
