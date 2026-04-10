// RPCProtocol implementation
#include "best_server/rpc/rpc_protocol.hpp"
#include <cstring>
#include <algorithm>
#include <zlib.h>

namespace best_server {
namespace rpc {

// RPCMessage implementation
RPCMessage::RPCMessage()
    : serialization_format_(SerializationFormat::JSON)
    , compression_algorithm_(CompressionAlgorithm::None)
    , encryption_algorithm_(EncryptionAlgorithm::None)
{
    memset(&header_, 0, sizeof(header_));
    header_.magic = 0x52504342;  // "RPCB"
    header_.version = ProtocolVersion(1, 0, 0);
    header_.message_id = 0;
}

RPCMessage::~RPCMessage() {
}

bool RPCMessage::has_flag(MessageFlags flag) const {
    return (header_.flags & static_cast<uint32_t>(flag)) != 0;
}

void RPCMessage::set_flag(MessageFlags flag, bool enable) {
    if (enable) {
        header_.flags |= static_cast<uint32_t>(flag);
    } else {
        header_.flags &= ~static_cast<uint32_t>(flag);
    }
}

memory::ZeroCopyBuffer RPCMessage::serialize() const {
    // Calculate total size
    size_t total_size = sizeof(RPCMessageHeader) + body_.size();
    
    memory::ZeroCopyBuffer buffer(total_size);
    
    // Calculate header checksum
    header_.header_checksum = calculate_header_checksum();
    header_.body_length = body_.size();
    
    // Write header (single memcpy for better performance)
    buffer.write(&header_, sizeof(RPCMessageHeader));
    
    // Write body using append to potentially avoid copy if same buffer
    if (body_.size() > 0) {
        buffer.append(body_);
    }
    
    return buffer;
}

bool RPCMessage::deserialize(const memory::ZeroCopyBuffer& buffer) {
    if (buffer.size() < sizeof(RPCMessageHeader)) {
        return false;
    }
    
    // Read header
    memcpy(&header_, buffer.data(), sizeof(RPCMessageHeader));
    
    // Validate header
    if (!validate_header()) {
        return false;
    }
    
    // Check body length
    if (buffer.size() < sizeof(RPCMessageHeader) + header_.body_length) {
        return false;
    }
    
    // Read body using slice for zero-copy when possible
    if (header_.body_length > 0) {
        // Create a slice to avoid copying the body data
        memory::ZeroCopyBuffer body_slice = buffer.slice(sizeof(RPCMessageHeader), header_.body_length);
        
        // If the slice is unique, we can use it directly
        if (body_slice.is_unique()) {
            body_ = std::move(body_slice);
        } else {
            // Otherwise, we need to copy
            body_.write(body_slice.data(), body_slice.size());
        }
    }
    
    return true;
}

uint32_t RPCMessage::calculate_header_checksum() const {
    // Simple checksum: sum of header bytes (excluding checksum field)
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&header_);
    uint32_t checksum = 0;
    
    for (size_t i = 0; i < offsetof(RPCMessageHeader, header_checksum); i++) {
        checksum += data[i];
    }
    
    return checksum;
}

bool RPCMessage::validate_header() const {
    // Check magic number
    if (header_.magic != 0x52504342) {
        return false;
    }
    
    // Check version compatibility
    ProtocolVersion current_version(1, 0, 0);
    if (!header_.version.compatible_with(current_version)) {
        return false;
    }
    
    // Validate checksum
    uint32_t expected_checksum = calculate_header_checksum();
    if (header_.header_checksum != expected_checksum) {
        return false;
    }
    
    return true;
}

// RPCProtocol implementation
RPCProtocol::RPCProtocol()
    : version_(1, 0, 0)
    , serialization_format_(SerializationFormat::JSON)
    , compression_algorithm_(CompressionAlgorithm::None)
    , compression_level_(-1)
    , encryption_algorithm_(EncryptionAlgorithm::None)
{
}

RPCProtocol::~RPCProtocol() {
}

void RPCProtocol::set_serialization_format(SerializationFormat format) {
    serialization_format_ = format;
}

void RPCProtocol::set_compression(CompressionAlgorithm algorithm, int level) {
    compression_algorithm_ = algorithm;
    compression_level_ = level;
}

void RPCProtocol::set_encryption(EncryptionAlgorithm algorithm, const std::string& key) {
    encryption_algorithm_ = algorithm;
    encryption_key_ = key;
}

memory::ZeroCopyBuffer RPCProtocol::serialize_request(const RPCRequest& request) {
    // Serialize request body
    memory::ZeroCopyBuffer body;
    
    switch (serialization_format_) {
        case SerializationFormat::JSON:
            body = serialize_json(request);
            break;
        case SerializationFormat::MessagePack:
            body = serialize_messagepack(request);
            break;
        case SerializationFormat::FlatBuffers:
        case SerializationFormat::Custom:
            body = serialize_flatbuffers(request);
            break;
        default:
            body = serialize_json(request);
            break;
    }
    
    // Compress if enabled
    if (compression_algorithm_ != CompressionAlgorithm::None) {
        body = compress(body);
    }
    
    // Encrypt if enabled
    if (encryption_algorithm_ != EncryptionAlgorithm::None) {
        body = encrypt(body);
    }
    
    // Create message
    RPCMessage message;
    message.header().message_type = 1;  // Request
    message.header().message_id = request.id;
    message.body() = std::move(body);
    message.set_serialization_format(serialization_format_);
    
    if (compression_algorithm_ != CompressionAlgorithm::None) {
        message.set_flag(MessageFlags::Compressed, true);
    }
    
    if (encryption_algorithm_ != EncryptionAlgorithm::None) {
        message.set_flag(MessageFlags::Encrypted, true);
    }
    
    return message.serialize();
}

bool RPCProtocol::deserialize_request(const memory::ZeroCopyBuffer& buffer, RPCRequest& request) {
    RPCMessage message;
    if (!message.deserialize(buffer)) {
        return false;
    }
    
    memory::ZeroCopyBuffer body = message.body();
    
    // Decrypt if needed
    if (message.has_flag(MessageFlags::Encrypted)) {
        body = decrypt(body);
    }
    
    // Decompress if needed
    if (message.has_flag(MessageFlags::Compressed)) {
        body = decompress(body);
    }
    
    // Deserialize based on format
    switch (message.serialization_format()) {
        case SerializationFormat::JSON:
            return deserialize_json(body, request);
        case SerializationFormat::MessagePack:
            return deserialize_messagepack(body, request);
        case SerializationFormat::FlatBuffers:
        case SerializationFormat::Custom:
            return deserialize_flatbuffers(body, request);
        default:
            return deserialize_json(body, request);
    }
}

memory::ZeroCopyBuffer RPCProtocol::serialize_response(const RPCResponse& response) {
    // Serialize response body
    memory::ZeroCopyBuffer body;
    
    switch (serialization_format_) {
        case SerializationFormat::JSON:
            body = serialize_json(response);
            break;
        case SerializationFormat::MessagePack:
            body = serialize_messagepack(response);
            break;
        case SerializationFormat::FlatBuffers:
        case SerializationFormat::Custom:
            body = serialize_flatbuffers(response);
            break;
        default:
            body = serialize_json(response);
            break;
    }
    
    // Compress if enabled
    if (compression_algorithm_ != CompressionAlgorithm::None) {
        body = compress(body);
    }
    
    // Encrypt if enabled
    if (encryption_algorithm_ != EncryptionAlgorithm::None) {
        body = encrypt(body);
    }
    
    // Create message
    RPCMessage message;
    message.header().message_type = 2;  // Response
    message.header().message_id = response.id;
    message.body() = std::move(body);
    message.set_serialization_format(serialization_format_);
    
    if (compression_algorithm_ != CompressionAlgorithm::None) {
        message.set_flag(MessageFlags::Compressed, true);
    }
    
    if (encryption_algorithm_ != EncryptionAlgorithm::None) {
        message.set_flag(MessageFlags::Encrypted, true);
    }
    
    return message.serialize();
}

bool RPCProtocol::deserialize_response(const memory::ZeroCopyBuffer& buffer, RPCResponse& response) {
    RPCMessage message;
    if (!message.deserialize(buffer)) {
        return false;
    }
    
    memory::ZeroCopyBuffer body = message.body();
    
    // Decrypt if needed
    if (message.has_flag(MessageFlags::Encrypted)) {
        body = decrypt(body);
    }
    
    // Decompress if needed
    if (message.has_flag(MessageFlags::Compressed)) {
        body = decompress(body);
    }
    
    // Deserialize based on format
    switch (message.serialization_format()) {
        case SerializationFormat::JSON:
            return deserialize_json(body, response);
        case SerializationFormat::MessagePack:
            return deserialize_messagepack(body, response);
        case SerializationFormat::FlatBuffers:
        case SerializationFormat::Custom:
            return deserialize_flatbuffers(body, response);
        default:
            return deserialize_json(body, response);
    }
}

memory::ZeroCopyBuffer RPCProtocol::compress(const memory::ZeroCopyBuffer& data) {
    if (data.empty()) {
        return memory::ZeroCopyBuffer();
    }
    
    switch (compression_algorithm_) {
        case CompressionAlgorithm::Gzip: {
            z_stream stream;
            memset(&stream, 0, sizeof(stream));
            
            if (deflateInit2(&stream, compression_level_, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
                return memory::ZeroCopyBuffer();
            }
            
            stream.avail_in = data.size();
            stream.next_in = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(data.data()));
            
            memory::ZeroCopyBuffer output(data.size() * 2);
            
            do {
                stream.avail_out = output.remaining();
                stream.next_out = reinterpret_cast<uint8_t*>(output.data() + output.size());
                
                int ret = deflate(&stream, Z_FINISH);
                if (ret == Z_STREAM_ERROR) {
                    deflateEnd(&stream);
                    return memory::ZeroCopyBuffer();
                }
                
                output.consumed(output.capacity() - stream.avail_out - output.size());
                
            } while (stream.avail_out == 0);
            
            deflateEnd(&stream);
            return output;
        }
        
        case CompressionAlgorithm::None:
        default:
            return data;
    }
}

memory::ZeroCopyBuffer RPCProtocol::decompress(const memory::ZeroCopyBuffer& data) {
    if (data.empty()) {
        return memory::ZeroCopyBuffer();
    }
    
    switch (compression_algorithm_) {
        case CompressionAlgorithm::Gzip: {
            z_stream stream;
            memset(&stream, 0, sizeof(stream));
            
            if (inflateInit2(&stream, 15 + 16) != Z_OK) {
                return memory::ZeroCopyBuffer();
            }
            
            stream.avail_in = data.size();
            stream.next_in = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(data.data()));
            
            memory::ZeroCopyBuffer output(data.size() * 4);
            
            do {
                stream.avail_out = output.remaining();
                stream.next_out = reinterpret_cast<uint8_t*>(output.data() + output.size());
                
                int ret = inflate(&stream, Z_FINISH);
                if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR) {
                    inflateEnd(&stream);
                    return memory::ZeroCopyBuffer();
                }
                
                output.consumed(output.capacity() - stream.avail_out - output.size());
                
            } while (stream.avail_out == 0);
            
            inflateEnd(&stream);
            return output;
        }
        
        case CompressionAlgorithm::None:
        default:
            return data;
    }
}

memory::ZeroCopyBuffer RPCProtocol::encrypt(const memory::ZeroCopyBuffer& data) {
    // Simplified encryption (XOR with key)
    if (data.empty() || encryption_key_.empty()) {
        return data;
    }
    
    memory::ZeroCopyBuffer result(data.size());
    const uint8_t* input = reinterpret_cast<const uint8_t*>(data.data());
    uint8_t* output = reinterpret_cast<uint8_t*>(result.data());
    
    for (size_t i = 0; i < data.size(); i++) {
        output[i] = input[i] ^ encryption_key_[i % encryption_key_.size()];
    }
    
    return result;
}

memory::ZeroCopyBuffer RPCProtocol::decrypt(const memory::ZeroCopyBuffer& data) {
    // Simplified decryption (XOR with key, same as encryption)
    return encrypt(data);
}

// JSON serialization (simplified)
memory::ZeroCopyBuffer RPCProtocol::serialize_json(const RPCRequest& request) {
    std::string json = R"({"id":)" + std::to_string(request.id) + 
                      R"(,"method":")" + request.method + 
                      R"(,"params":)" + request.params + "}";
    
    memory::ZeroCopyBuffer buffer(json.size());
    buffer.write(json.data(), json.size());
    return buffer;
}

memory::ZeroCopyBuffer RPCProtocol::serialize_json(const RPCResponse& response) {
    std::string json = R"({"id":)" + std::to_string(response.id) + 
                      R"(,"result":)" + response.result + 
                      R"(,"error":)" + response.error + "}";
    
    memory::ZeroCopyBuffer buffer(json.size());
    buffer.write(json.data(), json.size());
    return buffer;
}

bool RPCProtocol::deserialize_json(const memory::ZeroCopyBuffer& data, RPCRequest& request) {
    std::string json(data.data(), data.size());
    
    // Simplified JSON parsing
    // In a real implementation, you would use a proper JSON parser
    
    size_t id_pos = json.find("\"id\":");
    if (id_pos != std::string::npos) {
        size_t id_end = json.find(',', id_pos);
        if (id_end != std::string::npos) {
            std::string id_str = json.substr(id_pos + 5, id_end - id_pos - 5);
            request.id = std::stoull(id_str);
        }
    }
    
    size_t method_pos = json.find("\"method\":");
    if (method_pos != std::string::npos) {
        size_t method_start = json.find('"', method_pos + 9) + 1;
        size_t method_end = json.find('"', method_start);
        if (method_end != std::string::npos) {
            request.method = json.substr(method_start, method_end - method_start);
        }
    }
    
    size_t params_pos = json.find("\"params\":");
    if (params_pos != std::string::npos) {
        request.params = json.substr(params_pos + 9);
    }
    
    return true;
}

bool RPCProtocol::deserialize_json(const memory::ZeroCopyBuffer& data, RPCResponse& response) {
    std::string json(data.data(), data.size());
    
    // Simplified JSON parsing
    size_t id_pos = json.find("\"id\":");
    if (id_pos != std::string::npos) {
        size_t id_end = json.find(',', id_pos);
        if (id_end != std::string::npos) {
            std::string id_str = json.substr(id_pos + 5, id_end - id_pos - 5);
            response.id = std::stoull(id_str);
        }
    }
    
    size_t result_pos = json.find("\"result\":");
    if (result_pos != std::string::npos) {
        response.result = json.substr(result_pos + 9);
    }
    
    size_t error_pos = json.find("\"error\":");
    if (error_pos != std::string::npos) {
        response.error = json.substr(error_pos + 8);
    }
    
    return true;
}

// MessagePack serialization (placeholder)
memory::ZeroCopyBuffer RPCProtocol::serialize_messagepack(const RPCRequest& request) {
    // Placeholder for MessagePack serialization
    return serialize_json(request);
}

memory::ZeroCopyBuffer RPCProtocol::serialize_messagepack(const RPCResponse& response) {
    // Placeholder for MessagePack serialization
    return serialize_json(response);
}

bool RPCProtocol::deserialize_messagepack(const memory::ZeroCopyBuffer& data, RPCRequest& request) {
    // Placeholder for MessagePack deserialization
    return deserialize_json(data, request);
}

bool RPCProtocol::deserialize_messagepack(const memory::ZeroCopyBuffer& data, RPCResponse& response) {
    // Placeholder for MessagePack deserialization
    return deserialize_json(data, response);
}

// ProtocolHandler implementation
ProtocolHandler::ProtocolHandler() {
}

ProtocolHandler::~ProtocolHandler() {
}

void ProtocolHandler::set_protocol(std::shared_ptr<RPCProtocol> protocol) {
    protocol_ = protocol;
}

void ProtocolHandler::handle_data(const char* data, size_t size) {
    receive_buffer_.write(data, size);
    
    // Try to extract complete messages
    while (receive_buffer_.size() >= sizeof(RPCMessageHeader)) {
        RPCMessageHeader header;
        memcpy(&header, receive_buffer_.data(), sizeof(RPCMessageHeader));
        
        size_t total_size = sizeof(RPCMessageHeader) + header.body_length;
        
        if (receive_buffer_.size() < total_size) {
            break;  // Need more data
        }
        
        // Extract message
        memory::ZeroCopyBuffer message_buffer(total_size);
        memcpy(message_buffer.data(), receive_buffer_.data(), total_size);
        
        RPCMessage message;
        if (message.deserialize(message_buffer)) {
            stats_.messages_received++;
            stats_.bytes_received += total_size;
            
            if (message_callback_) {
                message_callback_(message);
            }
        }
        
        // Consume message
        receive_buffer_.consume(total_size);
    }
}

void ProtocolHandler::handle_data(const memory::ZeroCopyBuffer& buffer) {
    handle_data(buffer.data(), buffer.size());
}

void ProtocolHandler::set_message_callback(MessageCallback callback) {
    message_callback_ = callback;
}

void ProtocolHandler::reset() {
    receive_buffer_.clear();
    stats_ = ProtocolStats{};
}

// Zero-copy FlatBuffers serialization
memory::ZeroCopyBuffer RPCProtocol::serialize_flatbuffers(const RPCRequest& request) {
    // Simplified zero-copy serialization format
    // Format: [version(1)][method_len(2)][method][params_len(4)][params][id(8)]
    
    size_t method_len = request.method_name.size();
    size_t params_len = request.params.size();
    size_t total_size = 1 + 2 + method_len + 4 + params_len + 8;
    
    memory::ZeroCopyBuffer buffer(total_size);
    
    uint8_t* ptr = reinterpret_cast<uint8_t*>(buffer.data());
    
    // Version
    *ptr++ = 1;
    
    // Method name length
    *ptr++ = (method_len >> 8) & 0xFF;
    *ptr++ = method_len & 0xFF;
    
    // Method name
    if (method_len > 0) {
        std::memcpy(ptr, request.method_name.data(), method_len);
        ptr += method_len;
    }
    
    // Params length
    *ptr++ = (params_len >> 24) & 0xFF;
    *ptr++ = (params_len >> 16) & 0xFF;
    *ptr++ = (params_len >> 8) & 0xFF;
    *ptr++ = params_len & 0xFF;
    
    // Params
    if (params_len > 0) {
        std::memcpy(ptr, request.params.data(), params_len);
        ptr += params_len;
    }
    
    // ID
    uint64_t id = request.id;
    for (int i = 7; i >= 0; --i) {
        *ptr++ = (id >> (i * 8)) & 0xFF;
    }
    
    return buffer;
}

memory::ZeroCopyBuffer RPCProtocol::serialize_flatbuffers(const RPCResponse& response) {
    // Format: [version(1)][id(8)][has_result(1)][result_len(4)][result][has_error(1)][error_len(4)][error]
    
    size_t result_len = response.result.size();
    size_t error_len = response.error.size();
    bool has_result = !response.result.empty();
    bool has_error = !response.error.empty();
    
    size_t total_size = 1 + 8 + 1 + (has_result ? 4 + result_len : 0) + 1 + (has_error ? 4 + error_len : 0);
    
    memory::ZeroCopyBuffer buffer(total_size);
    
    uint8_t* ptr = reinterpret_cast<uint8_t*>(buffer.data());
    
    // Version
    *ptr++ = 1;
    
    // ID
    uint64_t id = response.id;
    for (int i = 7; i >= 0; --i) {
        *ptr++ = (id >> (i * 8)) & 0xFF;
    }
    
    // Has result
    *ptr++ = has_result ? 1 : 0;
    
    // Result
    if (has_result) {
        *ptr++ = (result_len >> 24) & 0xFF;
        *ptr++ = (result_len >> 16) & 0xFF;
        *ptr++ = (result_len >> 8) & 0xFF;
        *ptr++ = result_len & 0xFF;
        std::memcpy(ptr, response.result.data(), result_len);
        ptr += result_len;
    }
    
    // Has error
    *ptr++ = has_error ? 1 : 0;
    
    // Error
    if (has_error) {
        *ptr++ = (error_len >> 24) & 0xFF;
        *ptr++ = (error_len >> 16) & 0xFF;
        *ptr++ = (error_len >> 8) & 0xFF;
        *ptr++ = error_len & 0xFF;
        std::memcpy(ptr, response.error.data(), error_len);
        ptr += error_len;
    }
    
    return buffer;
}

bool RPCProtocol::deserialize_flatbuffers(const memory::ZeroCopyBuffer& data, RPCRequest& request) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());
    size_t remaining = data.size();
    
    if (remaining < 1) return false;
    
    // Version
    uint8_t version = *ptr++;
    remaining--;
    if (version != 1) return false;
    
    // Method name length
    if (remaining < 2) return false;
    uint16_t method_len = (ptr[0] << 8) | ptr[1];
    ptr += 2;
    remaining -= 2;
    
    // Method name
    if (remaining < method_len) return false;
    request.method_name.assign(reinterpret_cast<const char*>(ptr), method_len);
    ptr += method_len;
    remaining -= method_len;
    
    // Params length
    if (remaining < 4) return false;
    uint32_t params_len = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
    ptr += 4;
    remaining -= 4;
    
    // Params
    if (remaining < params_len) return false;
    request.params.assign(reinterpret_cast<const char*>(ptr), params_len);
    ptr += params_len;
    remaining -= params_len;
    
    // ID
    if (remaining < 8) return false;
    request.id = 0;
    for (int i = 0; i < 8; ++i) {
        request.id = (request.id << 8) | ptr[i];
    }
    
    return true;
}

bool RPCProtocol::deserialize_flatbuffers(const memory::ZeroCopyBuffer& data, RPCResponse& response) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());
    size_t remaining = data.size();
    
    if (remaining < 1) return false;
    
    // Version
    uint8_t version = *ptr++;
    remaining--;
    if (version != 1) return false;
    
    // ID
    if (remaining < 8) return false;
    response.id = 0;
    for (int i = 0; i < 8; ++i) {
        response.id = (response.id << 8) | ptr[i];
    }
    ptr += 8;
    remaining -= 8;
    
    // Has result
    if (remaining < 1) return false;
    bool has_result = (*ptr++ != 0);
    remaining--;
    
    // Result
    if (has_result) {
        if (remaining < 4) return false;
        uint32_t result_len = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
        ptr += 4;
        remaining -= 4;
        
        if (remaining < result_len) return false;
        response.result.assign(reinterpret_cast<const char*>(ptr), result_len);
        ptr += result_len;
        remaining -= result_len;
    }
    
    // Has error
    if (remaining < 1) return false;
    bool has_error = (*ptr++ != 0);
    remaining--;
    
    // Error
    if (has_error) {
        if (remaining < 4) return false;
        uint32_t error_len = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
        ptr += 4;
        remaining -= 4;
        
        if (remaining < error_len) return false;
        response.error.assign(reinterpret_cast<const char*>(ptr), error_len);
    }
    
    return true;
}

// Zero-copy binary serialization (alias for FlatBuffers)
memory::ZeroCopyBuffer RPCProtocol::serialize_binary(const RPCRequest& request) {
    return serialize_flatbuffers(request);
}

memory::ZeroCopyBuffer RPCProtocol::serialize_binary(const RPCResponse& response) {
    return serialize_flatbuffers(response);
}

bool RPCProtocol::deserialize_binary(const memory::ZeroCopyBuffer& data, RPCRequest& request) {
    return deserialize_flatbuffers(data, request);
}

bool RPCProtocol::deserialize_binary(const memory::ZeroCopyBuffer& data, RPCResponse& response) {
    return deserialize_flatbuffers(data, response);
}

} // namespace rpc
} // namespace best_server