// HTTP/2 Server implementation

#include "best_server/network/http2_server.hpp"
#include <cstring>
#include <algorithm>

namespace best_server {
namespace network {

// HTTP/2 stream implementation
HTTP2Stream::HTTP2Stream(uint32_t stream_id)
    : stream_id_(stream_id)
    , state_(State::IDLE)
    , send_window_(65535)
    , recv_window_(65535)
    , weight_(16)
    , error_code_(0)
{
}

HTTP2Stream::~HTTP2Stream() = default;

void HTTP2Stream::add_header(const std::string& name, const std::string& value) {
    headers_[name] = value;
}

void HTTP2Stream::append_data(const memory::ZeroCopyBuffer& data) {
    if (data.size() > 0) {
        data_.append(data);
    }
}

void HTTP2Stream::reset(uint32_t error_code) {
    error_code_ = error_code;
    state_ = State::CLOSED;
    data_.clear();
}

// HTTP/2 connection implementation
HTTP2Connection::HTTP2Connection(std::shared_ptr<io::TCPSocket> socket)
    : socket_(socket)
    , connection_window_(65535)
{
    // Initialize default settings
    settings_[0x1] = 4096;    // SETTINGS_HEADER_TABLE_SIZE
    settings_[0x2] = 1;       // SETTINGS_ENABLE_PUSH
    settings_[0x3] = 16384;   // SETTINGS_MAX_CONCURRENT_STREAMS
    settings_[0x4] = 65535;   // SETTINGS_INITIAL_WINDOW_SIZE
    settings_[0x5] = 16384;   // SETTINGS_MAX_FRAME_SIZE
    settings_[0x6] = 256;     // SETTINGS_MAX_HEADER_LIST_SIZE
    
    hpack_table_.max_size = 4096;
    hpack_table_.current_size = 0;
}

HTTP2Connection::~HTTP2Connection() = default;

bool HTTP2Connection::process_data(const memory::ZeroCopyBuffer& data) {
    if (data.empty()) {
        return false;
    }
    
    stats_.bytes_received += data.size();
    
    // Check for HTTP/2 preface
    if (handle_preface(data)) {
        return true;
    }
    
    // Parse frames
    size_t offset = 0;
    while (offset < data.size()) {
        if (offset + 9 > data.size()) {
            break;  // Need more data
        }
        
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data()) + offset;
        
        // Read frame header
        HTTP2Frame frame;
        frame.length = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];
        frame.type = static_cast<HTTP2FrameType>(ptr[3]);
        frame.flags = ptr[4];
        frame.stream_id = ((ptr[5] & 0x7F) << 24) | (ptr[6] << 16) | 
                        (ptr[7] << 8) | ptr[8];
        
        // Check if we have the complete frame
        if (offset + 9 + frame.length > data.size()) {
            break;  // Need more data
        }
        
        // Extract payload
        if (frame.length > 0) {
            frame.payload = data.slice(offset + 9, frame.length);
        }
        
        // Handle frame
        if (!handle_frame(frame)) {
            return false;
        }
        
        stats_.frames_received++;
        offset += 9 + frame.length;
    }
    
    return true;
}

bool HTTP2Connection::send_frame(const HTTP2Frame& frame) {
    // Serialize frame
    uint8_t header[9];
    
    header[0] = (frame.length >> 16) & 0xFF;
    header[1] = (frame.length >> 8) & 0xFF;
    header[2] = frame.length & 0xFF;
    header[3] = static_cast<uint8_t>(frame.type);
    header[4] = frame.flags;
    header[5] = (frame.stream_id >> 24) & 0x7F;
    header[6] = (frame.stream_id >> 16) & 0xFF;
    header[7] = (frame.stream_id >> 8) & 0xFF;
    header[8] = frame.stream_id & 0xFF;
    
    memory::ZeroCopyBuffer buffer(9 + frame.length);
    buffer.write(header, 9);
    
    if (frame.length > 0 && frame.payload.size() > 0) {
        buffer.append(frame.payload);
    }
    
    // Convert to vector<uint8_t> for async write
    std::vector<uint8_t> data(buffer.data(), buffer.data() + buffer.size());
    socket_->write_async(data).then([](size_t bytes_sent) {
        (void)bytes_sent;
    });
    
    stats_.frames_sent++;
    stats_.bytes_sent += buffer.size();
    
    return true;
}

HTTP2Stream* HTTP2Connection::get_stream(uint32_t stream_id) {
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        return it->second.get();
    }
    return nullptr;
}

HTTP2Stream* HTTP2Connection::create_stream(uint32_t stream_id) {
    if (streams_.count(stream_id) > 0) {
        return nullptr;
    }
    
    auto stream = std::make_unique<HTTP2Stream>(stream_id);
    stream->set_state(HTTP2Stream::State::OPEN);
    
    HTTP2Stream* ptr = stream.get();
    streams_[stream_id] = std::move(stream);
    
    stats_.streams_created++;
    return ptr;
}

void HTTP2Connection::close_stream(uint32_t stream_id, uint32_t error_code) {
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        it->second->reset(error_code);
        streams_.erase(it);
        stats_.streams_closed++;
    }
}

void HTTP2Connection::update_setting(uint16_t id, uint32_t value) {
    settings_[id] = value;
}

uint32_t HTTP2Connection::get_setting(uint16_t id) const {
    auto it = settings_.find(id);
    if (it != settings_.end()) {
        return it->second;
    }
    return 0;
}

void HTTP2Connection::update_connection_window(int32_t delta) {
    connection_window_ += delta;
}

bool HTTP2Connection::encode_headers(const std::map<std::string, std::string>& headers,
                                     memory::ZeroCopyBuffer& output) {
    // Simplified HPACK encoding
    // In a real implementation, this would use proper HPACK compression
    for (const auto& [name, value] : headers) {
        std::string line = name + ": " + value + "\r\n";
        output.write(line.data(), line.size());
    }
    return true;
}

bool HTTP2Connection::decode_headers(const memory::ZeroCopyBuffer& input,
                                     std::map<std::string, std::string>& headers) {
    // Simplified HPACK decoding
    std::string data(input.data(), input.size());
    
    size_t pos = 0;
    while (pos < data.size()) {
        size_t colon = data.find(':', pos);
        if (colon == std::string::npos) break;
        
        std::string name = data.substr(pos, colon - pos);
        size_t end = data.find("\r\n", colon);
        if (end == std::string::npos) break;
        
        std::string value = data.substr(colon + 1, end - colon - 1);
        
        // Trim whitespace
        size_t start = value.find_first_not_of(" \t");
        if (start != std::string::npos) {
            value = value.substr(start);
        }
        
        headers[name] = value;
        pos = end + 2;
    }
    
    return true;
}

bool HTTP2Connection::handle_preface(const memory::ZeroCopyBuffer& data) {
    // HTTP/2 connection preface: "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    static const uint8_t preface[] = {
        0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54, 0x54, 0x50,
        0x2f, 0x32, 0x2e, 0x30, 0x0d, 0x0a, 0x0d, 0x0a, 0x53, 0x4d,
        0x0d, 0x0a, 0x0d, 0x0a
    };
    
    if (data.size() >= 24 && 
        memcmp(data.data(), preface, 24) == 0) {
        // Send SETTINGS frame
        HTTP2Frame settings_frame;
        settings_frame.length = 0;
        settings_frame.type = HTTP2FrameType::SETTINGS;
        settings_frame.flags = 0;
        settings_frame.stream_id = 0;
        
        return send_frame(settings_frame);
    }
    
    return false;
}

bool HTTP2Connection::handle_frame(const HTTP2Frame& frame) {
    switch (frame.type) {
        case HTTP2FrameType::HEADERS:
            return handle_headers_frame(frame);
        case HTTP2FrameType::DATA:
            return handle_data_frame(frame);
        case HTTP2FrameType::SETTINGS:
            return handle_settings_frame(frame);
        case HTTP2FrameType::WINDOW_UPDATE:
            return handle_window_update_frame(frame);
        case HTTP2FrameType::RST_STREAM:
            return handle_rst_stream_frame(frame);
        case HTTP2FrameType::PING:
            return handle_ping_frame(frame);
        case HTTP2FrameType::GOAWAY:
            return handle_goaway_frame(frame);
        default:
            // Ignore unknown frames
            return true;
    }
}

bool HTTP2Connection::handle_headers_frame(const HTTP2Frame& frame) {
    HTTP2Stream* stream = get_stream(frame.stream_id);
    if (!stream) {
        stream = create_stream(frame.stream_id);
        if (!stream) {
            return false;
        }
    }
    
    // Decode headers
    std::map<std::string, std::string> headers;
    if (decode_headers(frame.payload, headers)) {
        for (const auto& [name, value] : headers) {
            stream->add_header(name, value);
        }
    }
    
    // Check END_STREAM flag
    if (frame.flags & 0x1) {
        stream->set_state(HTTP2Stream::State::HALF_CLOSED_REMOTE);
    }
    
    return true;
}

bool HTTP2Connection::handle_data_frame(const HTTP2Frame& frame) {
    HTTP2Stream* stream = get_stream(frame.stream_id);
    if (!stream) {
        return false;
    }
    
    // Append data
    stream->append_data(frame.payload);
    
    // Update receive window
    stream->update_recv_window(-static_cast<int32_t>(frame.length));
    
    // Check END_STREAM flag
    if (frame.flags & 0x1) {
        stream->set_state(HTTP2Stream::State::HALF_CLOSED_REMOTE);
    }
    
    return true;
}

bool HTTP2Connection::handle_settings_frame(const HTTP2Frame& frame) {
    if (frame.flags & 0x1) {  // ACK
        return true;
    }
    
    // Parse settings
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(frame.payload.data());
    for (size_t i = 0; i < frame.length; i += 6) {
        uint16_t id = (ptr[i] << 8) | ptr[i+1];
        uint32_t value = (ptr[i+2] << 24) | (ptr[i+3] << 16) | 
                       (ptr[i+4] << 8) | ptr[i+5];
        update_setting(id, value);
    }
    
    // Send ACK
    HTTP2Frame ack_frame;
    ack_frame.length = 0;
    ack_frame.type = HTTP2FrameType::SETTINGS;
    ack_frame.flags = 0x1;  // ACK
    ack_frame.stream_id = 0;
    
    return send_frame(ack_frame);
}

bool HTTP2Connection::handle_window_update_frame(const HTTP2Frame& frame) {
    if (frame.length != 4) {
        return false;
    }
    
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(frame.payload.data());
    uint32_t window_increment = (ptr[0] << 24) | (ptr[1] << 16) | 
                              (ptr[2] << 8) | ptr[3];
    
    if (frame.stream_id == 0) {
        update_connection_window(static_cast<int32_t>(window_increment));
    } else {
        HTTP2Stream* stream = get_stream(frame.stream_id);
        if (stream) {
            stream->update_send_window(static_cast<int32_t>(window_increment));
        }
    }
    
    return true;
}

bool HTTP2Connection::handle_rst_stream_frame(const HTTP2Frame& frame) {
    if (frame.length != 4) {
        return false;
    }
    
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(frame.payload.data());
    uint32_t error_code = (ptr[0] << 24) | (ptr[1] << 16) | 
                        (ptr[2] << 8) | ptr[3];
    
    close_stream(frame.stream_id, error_code);
    return true;
}

bool HTTP2Connection::handle_ping_frame(const HTTP2Frame& frame) {
    if (frame.length != 8) {
        return false;
    }
    
    // Send PING ACK
    HTTP2Frame ack_frame;
    ack_frame.length = 8;
    ack_frame.type = HTTP2FrameType::PING;
    ack_frame.flags = 0x1;  // ACK
    ack_frame.stream_id = 0;
    ack_frame.payload = frame.payload;
    
    return send_frame(ack_frame);
}

bool HTTP2Connection::handle_goaway_frame(const HTTP2Frame&) {
    // Connection is being closed by peer
    return true;
}

// HTTP/2 server implementation
HTTP2Server::HTTP2Server() : running_(false), enable_upgrade_(true) {
}

HTTP2Server::~HTTP2Server() {
    stop();
}

bool HTTP2Server::start(const std::string& address, uint16_t port) {
    acceptor_ = std::make_unique<io::TCPAcceptor>();
    
    io::SocketAddress addr(address, port);
    if (!acceptor_->bind(addr, 128)) {
        return false;
    }
    
    running_.store(true);
    accept_loop();
    
    return true;
}

void HTTP2Server::stop() {
    running_.store(false);
    
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    acceptor_.reset();
}

void HTTP2Server::register_handler(const std::string& path, HTTPRequestHandler handler) {
    handlers_[path] = handler;
}

void HTTP2Server::accept_loop() {
    // Use async accept with callback
    acceptor_->accept([this](std::shared_ptr<io::TCPSocket> socket, std::error_code ec) {
        if (!running_.load()) {
            return;
        }
        
        if (ec || !socket) {
            // Accept failed, try again
            if (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                accept_loop();
            }
            return;
        }
        
        handle_connection(std::move(socket));
        
        // Continue accepting
        if (running_.load()) {
            accept_loop();
        }
    });
}

void HTTP2Server::handle_connection(std::shared_ptr<io::TCPSocket> socket) {
    auto socket_addr = socket->remote_address().to_string();
    auto conn = std::make_unique<HTTP2Connection>(std::move(socket));
    
    // Store connection
    connections_[socket_addr] = std::move(conn);
    
    // Async read
    auto& socket_ref = *connections_[socket_addr]->socket();
    socket_ref.read_async(1024).then([this, socket_addr](memory::ZeroCopyBuffer buffer) {
        auto it = connections_.find(socket_addr);
        if (it != connections_.end()) {
            it->second->process_data(buffer);
        }
    });
}

bool HTTP2Server::push_stream(HTTP2Connection* conn, uint32_t parent_stream_id,
                             const std::string&, 
                             const std::map<std::string, std::string>& headers) {
    // Create PUSH_PROMISE frame
    HTTP2Frame frame;
    frame.type = HTTP2FrameType::PUSH_PROMISE;
    frame.flags = 0x4;  // END_HEADERS
    frame.stream_id = parent_stream_id;
    
    // Encode headers
    if (!conn->encode_headers(headers, frame.payload)) {
        return false;
    }
    
    return conn->send_frame(frame);
}

} // namespace network
} // namespace best_server