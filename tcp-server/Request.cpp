#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>


#include "Request.h"
#include "utils/serializer.h"
#include "utils/deserializable.h"

bytestream Payload :: serialize() const {
    bytestream payloadbytes;
    utils::write_int32(payloadbytes,timeout);

    payloadbytes.push_back(ack);

    utils::write_int32(payloadbytes, topics.size());
    for(const TopicData& topic : topics){
        std::string topicName = topic.topicName;
        utils::write_string(payloadbytes,topicName);

        std::vector<PartitionData> partitionData = topic.partitions;
        utils::write_int32(payloadbytes,partitionData.size());

        for(const PartitionData& partition : partitionData){
            utils::write_int16(payloadbytes,partition.partitionId);

        }
    }
    return payloadbytes;
}

Payload Payload :: deserialize(bytestream stream) {

}

bytestream Request::serialize() const {
    bytestream payloadStream = payload.serialize();
    int32_t paylod_size = payloadStream.size();

    bytestream clientstream;
    utils::write_string(clientstream,client_id);
    int32_t client_size = clientstream.size();

    int32_t total_size = paylod_size + client_size + 8;

    if(total_size > MAX_PAYLOAD_SIZE){
        throw std::runtime_error("paylod exceeded max_size");
    }
    bytestream stream;
    utils::write_int32(stream,total_size);
    utils::write_int16(stream,api_key);
    utils::write_int16(stream,api_version);
    utils::write_int32(stream,correlation_id);
    stream.insert(stream.end(),clientstream.begin(),clientstream.end());
    stream.insert(stream.end(),payloadStream.begin(),payloadStream.end());

    return stream;
}


Request Request::deserialize(bytestream stream) {
    utils::read
}