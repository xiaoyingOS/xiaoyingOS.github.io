// HTTP/2 Server - High-performance HTTP/2 implementation
//
// Implements HTTP/2 with:
// - Stream multiplexing
// - Header compression (HPACK)
// - Flow control
// - Server push
// - Prioritization
// - Zero-copy frame handling

#ifndef BEST_SERVER_NETWORK_HTTP2_SERVER_HPP
#define BEST_SERVER_NETWORK_HTTP2_SERVER_HPP

#include "best_server/network/http_server.hpp"
#include "best_server/memory/zero_copy_buffer.hpp"
#include <map>
#include <deque>
#include <atomic>

namespace best_server {
namespace network {

// HTTP/2 frame types
enum class HTTP2FrameType : uint8_t {
    DATA = 0x0,
    HEADERS = 0x1,
    PRIORITY = 0x2,
    RST_STREAM = 0x3,
    SETTINGS = 0x4,
    PUSH_PROMISE = 0x5,
    PING = 0x6,
    GOAWAY = 0x7,
    WINDOW_UPDATE = 0x8,
    CONTINUATION = 0x9,
    ALTSVC = 0xA,
    BLOCKED = 0xB
};

// HTTP/2 frame
struct HTTP2Frame {
    uint32_t length;          // 24-bit length
    HTTP2FrameType type;     // 8-bit type
    uint8_t flags;           // 8-bit flags
    uint32_t stream_id;      // 31-bit stream ID
    memory::ZeroCopyBuffer payload;
};

// HTTP/2 stream
class HTTP2Stream {
public:
    HTTP2Stream(uint32_t stream_id);
    ~HTTP2Stream();
    
    uint32_t stream_id() const { return stream_id_; }
    
    // State management
    enum class State {
        IDLE,
        RESERVED_LOCAL,
        RESERVED_REMOTE,
        OPEN,
        HALF_CLOSED_LOCAL,
        HALF_CLOSED_REMOTE,
        CLOSED
    };
    State state() const { return state_; }
    void set_state(State state) { state_ = state; }
    
    // Flow control
    size_t send_window() const { return send_window_; }
    size_t recv_window() const { return recv_window_; }
    void update_send_window(int32_t delta) { send_window_ += delta; }
    void update_recv_window(int32_t delta) { recv_window_ += delta; }
    
    // Headers
    void add_header(const std::string& name, const std::string& value);
    const std::map<std::string, std::string>& headers() const { return headers_; }
    void set_headers(const std::map<std::string, std::string>& headers) { headers_ = headers; }
    
    // Data
    void append_data(const memory::ZeroCopyBuffer& data);
    const memory::ZeroCopyBuffer& data() const { return data_; }
    
    // Priority
    uint8_t weight() const { return weight_; }
    void set_weight(uint8_t weight) { weight_ = weight; }
    
    // Reset stream
    void reset(uint32_t error_code);
    
private:
    uint32_t stream_id_;
    State state_;
    size_t send_window_;
    size_t recv_window_;
    std::map<std::string, std::string> headers_;
    memory::ZeroCopyBuffer data_;
    uint8_t weight_;
    uint32_t error_code_;
};

// HTTP/2 connection
class HTTP2Connection {
public:
    HTTP2Connection(std::shared_ptr<io::TCPSocket> socket);
    ~HTTP2Connection();
    
    // Get socket
    io::TCPSocket* socket() { return socket_.get(); }
    
    // Process incoming data
    bool process_data(const memory::ZeroCopyBuffer& data);
    
    // Send frame
    bool send_frame(const HTTP2Frame& frame);
    
    // Stream management
    HTTP2Stream* get_stream(uint32_t stream_id);
    HTTP2Stream* create_stream(uint32_t stream_id);
    void close_stream(uint32_t stream_id, uint32_t error_code = 0);
    
    // Settings
    void update_setting(uint16_t id, uint32_t value);
    uint32_t get_setting(uint16_t id) const;
    
    // HPACK (Header compression)
    bool encode_headers(const std::map<std::string, std::string>& headers, 
                       memory::ZeroCopyBuffer& output);
    bool decode_headers(const memory::ZeroCopyBuffer& input,
                       std::map<std::string, std::string>& headers);
    
    // Flow control
    void update_connection_window(int32_t delta);
    size_t connection_window() const { return connection_window_; }
    
    // Statistics
    struct Statistics {
        uint64_t streams_created;
        uint64_t streams_closed;
        uint64_t frames_received;
        uint64_t frames_sent;
        uint64_t bytes_received;
        uint64_t bytes_sent;
    };
    Statistics stats() const { return stats_; }
    
private:
    std::shared_ptr<io::TCPSocket> socket_;
    std::map<uint32_t, std::unique_ptr<HTTP2Stream>> streams_;
    std::map<uint16_t, uint32_t> settings_;
    size_t connection_window_;
    Statistics stats_;
    
    // HPACK dynamic table
    struct HPACKTable {
        std::deque<std::pair<std::string, std::string>> entries;
        size_t max_size;
        size_t current_size;
    };
    HPACKTable hpack_table_;
    
    // Preface handling
    bool handle_preface(const memory::ZeroCopyBuffer& data);
    
    // Frame handling
    bool handle_frame(const HTTP2Frame& frame);
    bool handle_headers_frame(const HTTP2Frame& frame);
    bool handle_data_frame(const HTTP2Frame& frame);
    bool handle_settings_frame(const HTTP2Frame& frame);
    bool handle_window_update_frame(const HTTP2Frame& frame);
    bool handle_rst_stream_frame(const HTTP2Frame& frame);
    bool handle_ping_frame(const HTTP2Frame& frame);
    bool handle_goaway_frame(const HTTP2Frame& frame);
};

// HTTP/2 server
class HTTP2Server {
public:
    HTTP2Server();
    ~HTTP2Server();
    
    // Start server
    bool start(const std::string& address, uint16_t port);
    
    // Stop server
    void stop();
    
    // Register handler
    void register_handler(const std::string& path, std::function<void(HTTPRequest&, HTTPResponse&)> handler);
    
    // Enable HTTP/2 upgrade
    void enable_upgrade(bool enable) { enable_upgrade_ = enable; }
    
    // Server push
    bool push_stream(HTTP2Connection* conn, uint32_t parent_stream_id,
                    const std::string& path, const std::map<std::string, std::string>& headers);
    
private:
    void accept_loop();
    void handle_connection(std::shared_ptr<io::TCPSocket> socket);
    
    std::unique_ptr<io::TCPAcceptor> acceptor_;
    std::atomic<bool> running_;
    std::map<std::string, std::function<void(HTTPRequest&, HTTPResponse&)>> handlers_;
    std::map<std::string, std::unique_ptr<HTTP2Connection>> connections_;
    bool enable_upgrade_;
    
    std::thread accept_thread_;
};

} // namespace network
} // namespace best_server

#endif // BEST_SERVER_NETWORK_HTTP2_SERVER_HPP