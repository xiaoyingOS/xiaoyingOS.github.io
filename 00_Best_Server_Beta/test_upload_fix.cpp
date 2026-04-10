#include <best_server/best_server.hpp>
#include <iostream>
#include <fstream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

using namespace best_server;
using namespace best_server::network;

// File upload handler
void handle_file_upload(HTTPRequest& req, HTTPResponse& res, const std::string& upload_path) {
    if (req.method() != HTTPMethod::POST) {
        res.set_status(HTTPStatus::MethodNotAllowed);
        res.set_text("Only POST method is allowed");
        return;
    }
    
    std::string filename = req.get_header("X-Filename", "upload.bin");
    std::string file_path = upload_path + "/" + filename;
    
    int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        res.set_status(HTTPStatus::InternalServerError);
        res.set_text("Failed to create file: " + std::string(strerror(errno)));
        return;
    }
    
    const auto& body = req.body();
    if (body.size() > 0) {
        ssize_t written = write(fd, body.data(), body.size());
        if (written < 0) {
            close(fd);
            res.set_status(HTTPStatus::InternalServerError);
            res.set_text("Failed to write file: " + std::string(strerror(errno)));
            return;
        }
    }
    
    close(fd);
    
    res.set_status(HTTPStatus::OK);
    res.set_json(R"({"message": "File uploaded successfully", "filename": ")" + filename + 
                 R"(", "size": )" + std::to_string(body.size()) + R"(})");
}

int main() {
    std::cout << "Test Upload Fix - Testing edge-triggered fix\n";
    std::cout << "=============================================\n";
    
    std::string upload_path = "/data/data/com.termux/files/home/Server/web/uploads";
    
    HTTPServer server;
    HTTPServerConfig config;
    config.address = "0.0.0.0";
    config.port = 9001;
    config.enable_http2 = false;
    config.enable_websocket = false;
    config.enable_compression = false;
    config.max_request_size = SIZE_MAX;
    config.max_requests_per_connection = 1;  // Disable keep-alive
    
    server.post("/upload", [upload_path](HTTPRequest& req, HTTPResponse& res) {
        handle_file_upload(req, res, upload_path);
    });
    
    if (!server.start(config)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Server started on port 9001\n";
    std::cout << "Upload endpoint: POST http://0.0.0.0:9001/upload\n";
    std::cout << "Server is running...\n";
    std::cout.flush();
    
    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    server.stop();
    return 0;
}