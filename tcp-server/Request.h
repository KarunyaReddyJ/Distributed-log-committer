#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "definitions.h"
#include "utils/serializable.h"


class PartitionData{
    public:
    int16_t partitionId;
    std::vector<uint8_t> messages;
};
class TopicData {
    
    public:
    std::string topicName;
    std::vector<PartitionData> partitions;
};

class Payload : public Serializable {
    int32_t timeout ;
    int8_t ack;
    std::vector<TopicData> topics;
    public:
        int16_t getTimeout(){
            return timeout;
        }
        int8_t getAck(){
            return ack;
        }
        explicit Payload(){};
        bytestream serialize() const override;
        static Payload deserialize(bytestream stream);
};

class Request : public Serializable {
private:
    
    int32_t message_size; // Total size of (Header + Payload)
    //=================================================
    //                     Header
    //=================================================
    int16_t api_key;
    int16_t api_version;
    int32_t correlation_id;
    std::string client_id; 
    //=================================================
    //                     Payload
    //=================================================
    Payload payload; // Raw bytes or a specific struct

public:
    // You will need a method to serialize this into a byte buffer
    // to send over a socket.

    bytestream serialize() const override ;
    static Request deserialize(bytestream stream)  ;
};
