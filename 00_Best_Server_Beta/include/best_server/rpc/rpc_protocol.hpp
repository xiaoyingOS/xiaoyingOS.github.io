// RPCProtocol - RPC protocol implementation
// 
// Implements the RPC protocol with:
// - Multiple serialization formats (JSON, MessagePack, Protobuf)
// - Protocol versioning
// - Compression support
// - Encryption support
// - Zero-copy data transfer

#ifndef BEST_SERVER_RPC_RPC_PROTOCOL_HPP
#define BEST_SERVER_RPC_RPC_PROTOCOL_HPP

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

#include "rpc/rpc_server.hpp"
#include "memory/zero_copy_buffer.hpp"

namespace best_server {
namespace rpc {

// Serialization format
enum class SerializationFormat {
    JSON,
    MessagePack,
    Protobuf,
    CBOR,
    FlatBuffers,  // Zero-copy serialization
    Custom
};

// Compression algorithm
enum class CompressionAlgorithm {
    None,
    Gzip,
    Snappy,
    LZ4,
    Zstd
};

// Encryption algorithm
enum class EncryptionAlgorithm {
    None,
    AES256_GCM,
    ChaCha20_Poly1305
};

// Protocol version
struct ProtocolVersion {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    
    ProtocolVersion(uint8_t maj = 1, uint8_t min = 0, uint8_t pat = 0)
        : major(maj), minor(min), patch(pat) {}
    
    uint32_t to_uint32() const {
        return (major << 16) | (minor << 8) | patch;
    }
    
    bool compatible_with(const ProtocolVersion& other) const {
        return major == other.major;
    }
};

// RPC message header
struct RPCMessageHeader {
    uint32_t magic;          // Magic number: 0x52504342 ("RPCB")
    ProtocolVersion version; // Protocol version
    uint32_t flags;          // Flags (compression, encryption, etc.)
    uint32_t message_type;   // Request or response
    uint64_t message_id;     // Message ID
    uint32_t body_length;    // Body length
    uint32_t header_checksum; // Header checksum
};

// RPC message flags
enum class MessageFlags : uint32_t {
    None = 0,
    Compressed = 0x01,
    Encrypted = 0x02,
    OneWay = 0x04,
    Batch = 0x08,
    Streaming = 0x10
};

inline MessageFlags operator|(MessageFlags a, MessageFlags b) {
    return static_cast<MessageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline MessageFlags operator&(MessageFlags a, MessageFlags b) {
    return static_cast<MessageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

// RPC message
class RPCMessage {
public:
    RPCMessage();
    ~RPCMessage();
    
    // Header
    const RPCMessageHeader& header() const { return header_; }
    RPCMessageHeader& header() { return header_; }
    
    // Body
    const memory::ZeroCopyBuffer& body() const { return body_; }
    memory::ZeroCopyBuffer& body() { return body_; }
    
    // Flags
    bool has_flag(MessageFlags flag) const;
    void set_flag(MessageFlags flag, bool enable);
    
    // Serialization format
    SerializationFormat serialization_format() const { return serialization_format_; }
    void set_serialization_format(SerializationFormat format) { serialization_format_ = format; }
    
    // Compression
    CompressionAlgorithm compression_algorithm() const { return compression_algorithm_; }
    void set_compression_algorithm(CompressionAlgorithm algo) { compression_algorithm_ = algo; }
    
    // Encryption
    EncryptionAlgorithm encryption_algorithm() const { return encryption_algorithm_; }
    void set_encryption_algorithm(EncryptionAlgorithm algo) { encryption_algorithm_ = algo; }
    
    // Serialize message
    memory::ZeroCopyBuffer serialize() const;
    
    // Deserialize message
    bool deserialize(const memory::ZeroCopyBuffer& buffer);
    
    // Calculate header checksum
    uint32_t calculate_header_checksum() const;
    
    // Validate header
    bool validate_header() const;
    
private:
    RPCMessageHeader header_;
    memory::ZeroCopyBuffer body_;
    SerializationFormat serialization_format_;
    CompressionAlgorithm compression_algorithm_;
    EncryptionAlgorithm encryption_algorithm_;
};

// RPC protocol
class RPCProtocol {
public:
    RPCProtocol();
    ~RPCProtocol();
    
    // Serialize request
    memory::ZeroCopyBuffer serialize_request(const RPCRequest& request);
    
    // Deserialize request
    bool deserialize_request(const memory::ZeroCopyBuffer& buffer, RPCRequest& request);
    
    // Serialize response
    memory::ZeroCopyBuffer serialize_response(const RPCResponse& response);
    
    // Deserialize response
    bool deserialize_response(const memory::ZeroCopyBuffer& buffer, RPCResponse& response);
    
    // Set serialization format
    void set_serialization_format(SerializationFormat format);
    SerializationFormat serialization_format() const { return serialization_format_; }
    
    // Set compression
    void set_compression(CompressionAlgorithm algorithm, int level = -1);
    CompressionAlgorithm compression_algorithm() const { return compression_algorithm_; }
    
    // Set encryption
    void set_encryption(EncryptionAlgorithm algorithm, const std::string& key);
    EncryptionAlgorithm encryption_algorithm() const { return encryption_algorithm_; }
    
    // Protocol version
    ProtocolVersion version() const { return version_; }
    void set_version(const ProtocolVersion& version) { version_ = version; }
    
    // Compress data
    memory::ZeroCopyBuffer compress(const memory::ZeroCopyBuffer& data);
    
    // Decompress data
    memory::ZeroCopyBuffer decompress(const memory::ZeroCopyBuffer& data);
    
    // Encrypt data
    memory::ZeroCopyBuffer encrypt(const memory::ZeroCopyBuffer& data);
    
    // Decrypt data
    memory::ZeroCopyBuffer decrypt(const memory::ZeroCopyBuffer& data);
    
private:
    // Serialization helpers
    memory::ZeroCopyBuffer serialize_json(const RPCRequest& request);
    memory::ZeroCopyBuffer serialize_json(const RPCResponse& response);
    
    bool deserialize_json(const memory::ZeroCopyBuffer& data, RPCRequest& request);
    bool deserialize_json(const memory::ZeroCopyBuffer& data, RPCResponse& response);
    
    memory::ZeroCopyBuffer serialize_messagepack(const RPCRequest& request);
    memory::ZeroCopyBuffer serialize_messagepack(const RPCResponse& response);
    
    bool deserialize_messagepack(const memory::ZeroCopyBuffer& data, RPCRequest& request);
    bool deserialize_messagepack(const memory::ZeroCopyBuffer& data, RPCResponse& response);
    
    // Zero-copy FlatBuffers serialization
    memory::ZeroCopyBuffer serialize_flatbuffers(const RPCRequest& request);
    memory::ZeroCopyBuffer serialize_flatbuffers(const RPCResponse& response);
    
    bool deserialize_flatbuffers(const memory::ZeroCopyBuffer& data, RPCRequest& request);
    bool deserialize_flatbuffers(const memory::ZeroCopyBuffer& data, RPCResponse& response);
    
    // Zero-copy binary protocol (custom optimized format)
    memory::ZeroCopyBuffer serialize_binary(const RPCRequest& request);
    memory::ZeroCopyBuffer serialize_binary(const RPCResponse& response);
    
    bool deserialize_binary(const memory::ZeroCopyBuffer& data, RPCRequest& request);
    bool deserialize_binary(const memory::ZeroCopyBuffer& data, RPCResponse& response);
    
    ProtocolVersion version_;
    SerializationFormat serialization_format_;
    CompressionAlgorithm compression_algorithm_;
    int compression_level_;
    EncryptionAlgorithm encryption_algorithm_;
    std::string encryption_key_;
};

// Protocol statistics
struct ProtocolStats {
    uint64_t messages_sent{0};
    uint64_t messages_received{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t compression_ratio{0}; // compressed size / original size
    uint64_t encryption_overhead{0};
};

// Protocol handler (for low-level message handling)
class ProtocolHandler {
public:
    using MessageCallback = std::function<void(const RPCMessage&)>;
    
    ProtocolHandler();
    ~ProtocolHandler();
    
    // Set protocol
    void set_protocol(std::shared_ptr<RPCProtocol> protocol);
    
    // Handle incoming data
    void handle_data(const char* data, size_t size);
    void handle_data(const memory::ZeroCopyBuffer& buffer);
    
    // Set message callback
    void set_message_callback(MessageCallback callback);
    
    // Get statistics
    const ProtocolStats& stats() const { return stats_; }
    
    // Reset
    void reset();
    
private:
    std::shared_ptr<RPCProtocol> protocol_;
    MessageCallback message_callback_;
    
    // Buffer for incomplete messages
    memory::ZeroCopyBuffer receive_buffer_;
    
    ProtocolStats stats_;
};

} // namespace rpc
} // namespace best_server

#endif // BEST_SERVER_RPC_RPC_PROTOCOL_HPP