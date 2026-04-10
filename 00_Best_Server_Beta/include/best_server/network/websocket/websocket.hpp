#pragma once

#include <best_server/core/scheduler.hpp>
#include <best_server/future/future.hpp>
#include <best_server/io/tcp_socket.hpp>
#include <string>
#include <functional>
#include <memory>
#include <optional>

namespace best_server {
namespace network {
namespace websocket {

// WebSocket frame opcode
enum class OpCode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

// WebSocket frame
struct Frame {
    bool fin{false};
    bool rsv1{false};
    bool rsv2{false};
    bool rsv3{false};
    OpCode opcode{OpCode::Text};
    bool masked{false};
    uint32_t masking_key{0};
    std::vector<uint8_t> payload;
    
    std::vector<uint8_t> serialize() const;
    static bool deserialize(const std::vector<uint8_t>& data, Frame& frame, size_t& consumed);
};

// WebSocket close status codes
enum class CloseCode : uint16_t {
    NormalClosure = 1000,
    GoingAway = 1001,
    ProtocolError = 1002,
    UnsupportedData = 1003,
    NoStatusReceived = 1005,
    AbnormalClosure = 1006,
    InvalidFramePayloadData = 1007,
    PolicyViolation = 1008,
    MessageTooBig = 1009,
    MandatoryExtension = 1010,
    InternalServerError = 1011,
    ServiceRestart = 1012,
    TryAgainLater = 1013,
    BadGateway = 1014,
    TLSHandshake = 1015
};

// WebSocket configuration
struct Config {
    uint32_t max_message_size{16 * 1024 * 1024};  // 16MB
    uint32_t fragment_threshold{16 * 1024};       // 16KB
    bool enable_compression{false};
    uint32_t ping_interval{30000};                 // 30 seconds
    uint32_t ping_timeout{5000};                   // 5 seconds
    std::optional<std::string> subprotocols;
};

// WebSocket message
class Message {
public:
    Message() = default;
    explicit Message(OpCode opcode, std::string data);
    explicit Message(OpCode opcode, std::vector<uint8_t> data);
    
    OpCode opcode() const { return opcode_; }
    bool is_text() const { return opcode_ == OpCode::Text; }
    bool is_binary() const { return opcode_ == OpCode::Binary; }
    bool is_control() const { 
        return opcode_ >= OpCode::Close && opcode_ <= OpCode::Pong;
    }
    
    const std::string& text() const { return text_data_; }
    const std::vector<uint8_t>& binary() const { return binary_data_; }
    
    void set_text(const std::string& data);
    void set_binary(const std::vector<uint8_t>& data);
    
private:
    OpCode opcode_{OpCode::Text};
    std::string text_data_;
    std::vector<uint8_t> binary_data_;
};

// WebSocket handler interface
class Handler {
public:
    virtual ~Handler() = default;
    
    virtual void on_open() {}
    virtual void on_message(const Message& message) = 0;
    virtual void on_close(CloseCode code, const std::string& reason) {
        (void)code;
        (void)reason;
    }
    virtual void on_error(const std::string& error) {
        (void)error;
    }
    virtual void on_ping(const std::string& data) {
        (void)data;
        send_pong(data);
    }
    virtual void on_pong(const std::string& data) {
        (void)data;
    }
    
    virtual void send(const Message& message) = 0;
    virtual void send_pong(const std::string& data) = 0;
    virtual void close(CloseCode code = CloseCode::NormalClosure, 
                       const std::string& reason = "") = 0;
};

// WebSocket connection
class Connection : public Handler, public std::enable_shared_from_this<Connection> {
public:
    using Ptr = std::shared_ptr<Connection>;
    
    static future::Future<Ptr> accept(io::TCPSocket::Ptr socket, 
                                       const Config& config = Config());
    
    static future::Future<Ptr> connect(const std::string& url,
                                       const Config& config = Config());
    
    ~Connection() override;
    
    void on_message(const Message& message) override;
    void send(const Message& message) override;
    void send_pong(const std::string& data) override;
    void close(CloseCode code = CloseCode::NormalClosure,
               const std::string& reason = "") override;
    
    void set_handler(std::unique_ptr<Handler> handler);
    const std::string& remote_address() const;
    const std::string& path() const;
    
    // Get the remote address as string
    std::string get_remote_address() const;
    
private:
    Connection(io::TCPSocket::Ptr socket, const Config& config);
    
    void start_reading();
    void handle_frame(const Frame& frame);
    void handle_close_frame(const Frame& frame);
    void handle_data_frame(const Frame& frame);
    void handle_ping_frame(const Frame& frame);
    void handle_pong_frame(const Frame& frame);
    
    void send_frame(const Frame& frame);
    void send_text(const std::string& data);
    void send_binary(const std::vector<uint8_t>& data);
    
    static bool perform_handshake(io::TCPSocket::Ptr socket,
                                  const std::string& path,
                                  bool is_server,
                                  std::string& accepted_protocol);
    
    io::TCPSocket::Ptr socket_;
    Config config_;
    std::unique_ptr<Handler> handler_;
    std::string path_;
    std::string accepted_protocol_;
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> fragmented_data_;
    OpCode fragmented_opcode_{OpCode::Text};
    bool closing_{false};
};

// WebSocket server
class Server {
public:
    using ConnectionCallback = std::function<void(Connection::Ptr)>;
    
    Server(const std::string& address, uint16_t port, const Config& config = Config());
    ~Server();
    
    void set_connection_callback(ConnectionCallback callback);
    void start();
    void stop();
    
    const Config& config() const { return config_; }
    size_t connection_count() const;
    
private:
    void accept_connection();
    
    std::string address_;
    [[maybe_unused]] uint16_t port_;
    Config config_;
    std::unique_ptr<io::TCPAcceptor> listener_;
    ConnectionCallback connection_callback_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> connection_count_{0};
};

// WebSocket client
class Client {
public:
    Client(const Config& config = Config());
    ~Client();
    
    future::Future<Connection::Ptr> connect(const std::string& url);
    
    const Config& config() const { return config_; }
    
private:
    Config config_;
};

// WebSocket utilities
namespace utils {
    std::string generate_accept_key(const std::string& key);
    std::string generate_key();
    std::string encode_base64(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decode_base64(const std::string& data);
    std::string url_encode(const std::string& str);
    std::string url_decode(const std::string& str);
}

} // namespace websocket
} // namespace network
} // namespace best_server