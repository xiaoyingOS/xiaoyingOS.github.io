// WebSocket Implementation
// Supports RFC 6455 - The WebSocket Protocol

#include <best_server/network/websocket/websocket.hpp>
#include <best_server/core/reactor.hpp>
#include <best_server/utils/random.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <openssl/sha.h>

namespace best_server {
namespace network {
namespace websocket {

namespace {
    [[maybe_unused]] const char* WS_MAGIC_KEY = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const std::string WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
}

// Frame serialization
std::vector<uint8_t> Frame::serialize() const {
    std::vector<uint8_t> result;
    
    uint8_t first_byte = 0;
    if (fin) first_byte |= 0x80;
    if (rsv1) first_byte |= 0x40;
    if (rsv2) first_byte |= 0x20;
    if (rsv3) first_byte |= 0x10;
    first_byte |= static_cast<uint8_t>(opcode);
    result.push_back(first_byte);
    
    uint8_t second_byte = 0;
    if (masked) second_byte |= 0x80;
    
    size_t payload_len = payload.size();
    if (payload_len < 126) {
        second_byte |= static_cast<uint8_t>(payload_len);
        result.push_back(second_byte);
    } else if (payload_len < 65536) {
        second_byte |= 126;
        result.push_back(second_byte);
        uint16_t len16 = static_cast<uint16_t>(payload_len);
        result.push_back(static_cast<uint8_t>((len16 >> 8) & 0xFF));
        result.push_back(static_cast<uint8_t>(len16 & 0xFF));
    } else {
        second_byte |= 127;
        result.push_back(second_byte);
        uint64_t len64 = payload_len;
        for (int i = 7; i >= 0; --i) {
            result.push_back(static_cast<uint8_t>((len64 >> (i * 8)) & 0xFF));
        }
    }
    
    if (masked) {
        uint8_t mask_bytes[4];
        *reinterpret_cast<uint32_t*>(mask_bytes) = masking_key;
        result.insert(result.end(), mask_bytes, mask_bytes + 4);
        
        std::vector<uint8_t> masked_payload(payload.size());
        for (size_t i = 0; i < payload.size(); ++i) {
            masked_payload[i] = payload[i] ^ mask_bytes[i % 4];
        }
        result.insert(result.end(), masked_payload.begin(), masked_payload.end());
    } else {
        result.insert(result.end(), payload.begin(), payload.end());
    }
    
    return result;
}

// Frame deserialization
bool Frame::deserialize(const std::vector<uint8_t>& data, Frame& frame, size_t& consumed) {
    if (data.size() < 2) {
        return false;
    }
    
    consumed = 0;
    
    uint8_t first_byte = data[consumed++];
    frame.fin = (first_byte & 0x80) != 0;
    frame.rsv1 = (first_byte & 0x40) != 0;
    frame.rsv2 = (first_byte & 0x20) != 0;
    frame.rsv3 = (first_byte & 0x10) != 0;
    frame.opcode = static_cast<OpCode>(first_byte & 0x0F);
    
    uint8_t second_byte = data[consumed++];
    frame.masked = (second_byte & 0x80) != 0;
    uint8_t len_byte = second_byte & 0x7F;
    
    size_t payload_len = 0;
    if (len_byte < 126) {
        payload_len = len_byte;
    } else if (len_byte == 126) {
        if (data.size() < consumed + 2) return false;
        payload_len = (static_cast<uint16_t>(data[consumed]) << 8) | data[consumed + 1];
        consumed += 2;
    } else {
        if (data.size() < consumed + 8) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | data[consumed++];
        }
    }
    
    if (frame.masked) {
        if (data.size() < consumed + 4) return false;
        frame.masking_key = *reinterpret_cast<const uint32_t*>(&data[consumed]);
        consumed += 4;
    }
    
    if (data.size() < consumed + payload_len) return false;
    
    frame.payload.assign(data.begin() + consumed, data.begin() + consumed + payload_len);
    consumed += payload_len;
    
    if (frame.masked) {
        uint8_t* mask_bytes = reinterpret_cast<uint8_t*>(&frame.masking_key);
        for (size_t i = 0; i < frame.payload.size(); ++i) {
            frame.payload[i] ^= mask_bytes[i % 4];
        }
    }
    
    return true;
}

// Message implementation
Message::Message(OpCode opcode, std::string data)
    : opcode_(opcode) {
    if (opcode == OpCode::Text) {
        text_data_ = std::move(data);
    } else {
        binary_data_.assign(data.begin(), data.end());
    }
}

Message::Message(OpCode opcode, std::vector<uint8_t> data)
    : opcode_(opcode), binary_data_(std::move(data)) {
}

void Message::set_text(const std::string& data) {
    opcode_ = OpCode::Text;
    text_data_ = data;
    binary_data_.clear();
}

void Message::set_binary(const std::vector<uint8_t>& data) {
    opcode_ = OpCode::Binary;
    binary_data_ = data;
    text_data_.clear();
}

// Connection implementation
Connection::Connection(io::TCPSocket::Ptr socket, const Config& config)
    : socket_(std::move(socket)), config_(config) {
}

Connection::~Connection() {
    close();
}

future::Future<Connection::Ptr> Connection::accept(io::TCPSocket::Ptr socket,
                                                     const Config& config) {
    return future::make_future<Connection::Ptr>([socket, config]() {
        auto conn = std::shared_ptr<Connection>(new Connection(socket, config));
        
        // Perform WebSocket handshake
        std::string accepted_protocol;
        if (!perform_handshake(socket, "", true, accepted_protocol)) {
            throw std::runtime_error("WebSocket handshake failed");
        }
        
        conn->accepted_protocol_ = accepted_protocol;
        conn->start_reading();
        
        return conn;
    });
}

future::Future<Connection::Ptr> Connection::connect(const std::string& url,
                                                     const Config& config) {
    return future::make_future<Connection::Ptr>([url, config]() {
        // Parse URL
        size_t proto_end = url.find("://");
        if (proto_end == std::string::npos) {
            throw std::runtime_error("Invalid WebSocket URL");
        }
        
        std::string proto = url.substr(0, proto_end);
        std::string rest = url.substr(proto_end + 3);
        
        bool use_ssl = (proto == "wss");
        uint16_t default_port = use_ssl ? 443 : 80;
        
        size_t path_start = rest.find('/');
        std::string host_port = rest.substr(0, path_start);
        std::string path = (path_start != std::string::npos) ? 
                          rest.substr(path_start) : "/";
        
        size_t port_pos = host_port.find(':');
        std::string host = host_port.substr(0, port_pos);
        uint16_t port = default_port;
        if (port_pos != std::string::npos) {
            port = static_cast<uint16_t>(std::stoul(host_port.substr(port_pos + 1)));
        }
        
        // Create TCP connection
        auto socket = io::TCPSocket::create();
        if (!socket->connect_sync(host, port)) {
            throw std::runtime_error("Failed to connect to " + host + ":" + std::to_string(port));
        }
        
        auto conn = std::shared_ptr<Connection>(new Connection(socket, config));
        conn->path_ = path;
        
        // Perform client handshake
        std::string accepted_protocol;
        if (!perform_handshake(socket, path, false, accepted_protocol)) {
            throw std::runtime_error("WebSocket handshake failed");
        }
        
        conn->accepted_protocol_ = accepted_protocol;
        conn->start_reading();
        
        return conn;
    });
}

void Connection::start_reading() {
    auto self = shared_from_this();
    
    auto read_task = [self]() -> future::Future<void> {
        while (!self->closing_) {
            auto buffer = co_await self->socket_->read_async(65536);
            self->read_buffer_.insert(self->read_buffer_.end(),
                                      buffer.data(), buffer.data() + buffer.size());
            
            // Process frames
            size_t consumed = 0;
            while (consumed < self->read_buffer_.size()) {
                Frame frame;
                size_t frame_consumed = 0;
                
                if (Frame::deserialize(self->read_buffer_, frame, frame_consumed)) {
                    consumed += frame_consumed;
                    self->handle_frame(frame);
                } else {
                    break; // Incomplete frame, wait for more data
                }
            }
            
            if (consumed > 0) {
                self->read_buffer_.erase(self->read_buffer_.begin(),
                                        self->read_buffer_.begin() + consumed);
            }
        }
    };
    
    // Start the async read task
    read_task();
}

void Connection::handle_frame(const Frame& frame) {
    switch (frame.opcode) {
        case OpCode::Text:
        case OpCode::Binary:
            handle_data_frame(frame);
            break;
        case OpCode::Continuation:
            handle_data_frame(frame);
            break;
        case OpCode::Close:
            handle_close_frame(frame);
            break;
        case OpCode::Ping:
            handle_ping_frame(frame);
            break;
        case OpCode::Pong:
            handle_pong_frame(frame);
            break;
        default:
            if (handler_) {
                handler_->on_error("Invalid opcode");
            }
            break;
    }
}

void Connection::handle_data_frame(const Frame& frame) {
    if (frame.fin) {
        // Complete message
        Message message;
        if (frame.opcode == OpCode::Text || frame.opcode == OpCode::Continuation) {
            if (!fragmented_data_.empty()) {
                fragmented_data_.insert(fragmented_data_.end(),
                                       frame.payload.begin(),
                                       frame.payload.end());
                message.set_text(std::string(fragmented_data_.begin(),
                                             fragmented_data_.end()));
                fragmented_data_.clear();
            } else {
                message.set_text(std::string(frame.payload.begin(),
                                             frame.payload.end()));
            }
        } else {
            if (!fragmented_data_.empty()) {
                fragmented_data_.insert(fragmented_data_.end(),
                                       frame.payload.begin(),
                                       frame.payload.end());
                message.set_binary(fragmented_data_);
                fragmented_data_.clear();
            } else {
                message.set_binary(frame.payload);
            }
        }
        
        if (handler_) {
            handler_->on_message(message);
        }
    } else {
        // Fragmented message
        if (fragmented_data_.empty()) {
            fragmented_opcode_ = frame.opcode;
        }
        fragmented_data_.insert(fragmented_data_.end(),
                               frame.payload.begin(),
                               frame.payload.end());
    }
}

void Connection::handle_close_frame(const Frame& frame) {
    closing_ = true;
    
    CloseCode code = CloseCode::NormalClosure;
    std::string reason;
    
    if (frame.payload.size() >= 2) {
        code = static_cast<CloseCode>(
            (static_cast<uint16_t>(frame.payload[0]) << 8) | frame.payload[1]
        );
        if (frame.payload.size() > 2) {
            reason = std::string(frame.payload.begin() + 2, frame.payload.end());
        }
    }
    
    // Send close response
    Frame close_frame;
    close_frame.fin = true;
    close_frame.opcode = OpCode::Close;
    close_frame.payload = frame.payload;
    send_frame(close_frame);
    
    if (handler_) {
        handler_->on_close(code, reason);
    }
    
    socket_->close();
}

void Connection::handle_ping_frame(const Frame& frame) {
    if (handler_) {
        std::string data(frame.payload.begin(), frame.payload.end());
        handler_->on_ping(data);
    }
}

void Connection::handle_pong_frame(const Frame& frame) {
    if (handler_) {
        std::string data(frame.payload.begin(), frame.payload.end());
        handler_->on_pong(data);
    }
}

void Connection::send_frame(const Frame& frame) {
    auto data = frame.serialize();
    socket_->write_async(data).get();
}

void Connection::send_text(const std::string& data) {
    Frame frame;
    frame.fin = true;
    frame.opcode = OpCode::Text;
    frame.payload.assign(data.begin(), data.end());
    send_frame(frame);
}

void Connection::send_binary(const std::vector<uint8_t>& data) {
    Frame frame;
    frame.fin = true;
    frame.opcode = OpCode::Binary;
    frame.payload = data;
    send_frame(frame);
}

void Connection::send(const Message& message) {
    if (message.is_text()) {
        send_text(message.text());
    } else {
        send_binary(message.binary());
    }
}

void Connection::send_pong(const std::string& data) {
    Frame frame;
    frame.fin = true;
    frame.opcode = OpCode::Pong;
    frame.payload.assign(data.begin(), data.end());
    send_frame(frame);
}

void Connection::close(CloseCode code, const std::string& reason) {
    if (closing_) return;
    closing_ = true;
    
    Frame frame;
    frame.fin = true;
    frame.opcode = OpCode::Close;
    
    if (code != CloseCode::NoStatusReceived || !reason.empty()) {
        uint16_t code16 = static_cast<uint16_t>(code);
        frame.payload.push_back(static_cast<uint8_t>((code16 >> 8) & 0xFF));
        frame.payload.push_back(static_cast<uint8_t>(code16 & 0xFF));
        frame.payload.insert(frame.payload.end(), reason.begin(), reason.end());
    }
    
    send_frame(frame);
    socket_->close();
}

void Connection::set_handler(std::unique_ptr<Handler> handler) {
    handler_ = std::move(handler);
    if (handler_) {
        handler_->on_open();
    }
}

const std::string& Connection::remote_address() const {
    static std::string empty;
    if (!socket_) return empty;
    static thread_local std::string addr_str;
    addr_str = socket_->remote_address().to_string();
    return addr_str;
}

std::string Connection::get_remote_address() const {
    if (!socket_) return "";
    return socket_->remote_address().to_string();
}

const std::string& Connection::path() const {
    return path_;
}

[[maybe_unused]] static bool perform_handshake(io::TCPSocket::Ptr socket,
                                  const std::string& path,
                                  bool is_server,
                                  [[maybe_unused]] std::string& accepted_protocol) {
    if (is_server) {
        // Server-side handshake
        char buffer[4096];
        auto result = socket->read_async(sizeof(buffer)).get();
        
        std::string request(result.data(), result.size());
        
        // Parse GET request
        size_t get_pos = request.find("GET ");
        if (get_pos == std::string::npos) return false;
        
        size_t http_pos = request.find(" HTTP/1.1\r\n", get_pos);
        if (http_pos == std::string::npos) return false;
        
        std::string request_path = request.substr(get_pos + 4, http_pos - get_pos - 4);
        
        // Find WebSocket key
        size_t key_pos = request.find("\r\nSec-WebSocket-Key: ");
        if (key_pos == std::string::npos) return false;
        
        size_t key_end = request.find("\r\n", key_pos + 23);
        std::string client_key = request.substr(key_pos + 23, key_end - key_pos - 23);
        
        // Generate accept key
        std::string accept_key = utils::generate_accept_key(client_key);
        
        // Send response
        std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n"
            "\r\n";
        
        socket->write_async(std::vector<uint8_t>(response.begin(), response.end())).get();
        
        return true;
    } else {
        // Client-side handshake
        std::string key = utils::generate_key();
        
        std::string request =
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + socket->remote_address().to_string() + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + key + "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";
        
        socket->write_async(std::vector<uint8_t>(request.begin(), request.end())).get();
        
        // Read response
        char buffer[4096];
        auto result = socket->read_async(sizeof(buffer)).get();
        std::string response(result.data(), result.size());
        
        // Verify response
        if (response.find("HTTP/1.1 101") == std::string::npos) return false;
        if (response.find("Upgrade: websocket") == std::string::npos) return false;
        
        size_t accept_pos = response.find("\r\nSec-WebSocket-Accept: ");
        if (accept_pos == std::string::npos) return false;
        
        size_t accept_end = response.find("\r\n", accept_pos + 24);
        std::string server_accept = response.substr(accept_pos + 24, accept_end - accept_pos - 24);
        
        std::string expected_accept = utils::generate_accept_key(key);
        
        return server_accept == expected_accept;
    }
}

// Server implementation
Server::Server(const std::string& address, uint16_t port, const Config& config)
    : address_(address), port_(port), config_(config) {
    listener_ = std::make_unique<io::TCPAcceptor>();
    io::SocketAddress addr(address, port);
    listener_->bind(addr, 1024);
}

Server::~Server() {
    stop();
}

void Server::set_connection_callback(ConnectionCallback callback) {
    connection_callback_ = std::move(callback);
}

void Server::start() {
    running_ = true;
    accept_connection();
}

void Server::stop() {
    running_ = false;
    if (listener_) {
        listener_->stop();
    }
}

void Server::accept_connection() {
    if (!running_) return;
    
    listener_->accept([this](std::shared_ptr<io::TCPSocket> socket, std::error_code ec) {
        if (ec) {
            if (running_) {
                accept_connection();
            }
            return;
        }
        
        auto socket_ptr = socket;
        connection_count_++;
        
        Connection::accept(socket_ptr, config_).then([this](auto conn) {
            if (connection_callback_) {
                connection_callback_(conn);
            }
        });
        
        accept_connection();
    });
}

size_t Server::connection_count() const {
    return connection_count_.load();
}

// Client implementation
Client::Client(const Config& config)
    : config_(config) {
}

Client::~Client() = default;

future::Future<Connection::Ptr> Client::connect(const std::string& url) {
    return Connection::connect(url, config_);
}

// Utility functions
namespace utils {

std::string generate_accept_key(const std::string& key) {
    std::string combined = key + WS_GUID;
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(combined.c_str()),
           combined.size(),
           hash);
    
    return encode_base64(std::vector<uint8_t>(hash, hash + 20));
}

std::string generate_key() {
    std::vector<uint8_t> data(16);
    ::best_server::utils::Random::generate_bytes(data, data.size());
    return encode_base64(data);
}

std::string encode_base64(const std::vector<uint8_t>& data) {
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string result;
    result.reserve((data.size() * 4 + 2) / 3);
    
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t triple = (data[i] << 16) |
                         (i + 1 < data.size() ? (data[i + 1] << 8) : 0) |
                         (i + 2 < data.size() ? data[i + 2] : 0);
        
        result.push_back(base64_chars[(triple >> 18) & 0x3F]);
        result.push_back(base64_chars[(triple >> 12) & 0x3F]);
        result.push_back(base64_chars[(triple >> 6) & 0x3F]);
        result.push_back(base64_chars[triple & 0x3F]);
    }
    
    // Padding
    while (result.size() % 4) {
        result[result.size() - 1] = '=';
    }
    
    return result;
}

std::vector<uint8_t> decode_base64(const std::string& encoded) {
    static const int base64_chars[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    
    std::vector<uint8_t> result;
    result.reserve((encoded.size() * 3) / 4);
    
    uint32_t buffer = 0;
    int bits = 0;
    
    for (char c : encoded) {
        if (c == '=') break;
        
        int value = base64_chars[static_cast<unsigned char>(c)];
        if (value < 0) continue;
        
        buffer = (buffer << 6) | value;
        bits += 6;
        
        if (bits >= 8) {
            result.push_back(static_cast<uint8_t>((buffer >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    }
    
    return result;
}

std::string url_encode(const std::string& str) {
    std::string result;
    result.reserve(str.size() * 3);
    
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        }
    }
    
    return result;
}

std::string url_decode(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            char buf[3] = {str[i + 1], str[i + 2], 0};
            result += static_cast<char>(strtol(buf, nullptr, 16));
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    
    return result;
}

} // namespace utils

} // namespace websocket
} // namespace network
} // namespace best_server