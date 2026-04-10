// Web Server Daemon - Background Service
// 
// Runs continuously without interactive input

#include <best_server/best_server.hpp>
#include <best_server/network/websocket/websocket.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <csignal>
#include <atomic>

using namespace best_server;
using namespace best_server::network;

namespace fs = std::filesystem;

std::atomic<bool> running(true);

void signal_handler(int signal) {
    (void)signal;
    running = false;
}

// Static file serving
void serve_static_file(HTTPRequest& req, HTTPResponse& res, const std::string& base_path) {
    std::string url_path = req.path();
    if (url_path == "/") url_path = "/index.html";
    
    std::string file_path = base_path + url_path;
    
    // Security check
    if (file_path.find("..") != std::string::npos) {
        res.set_status(HTTPStatus::Forbidden);
        res.set_text("Access denied");
        return;
    }
    
    // Check if file exists
    if (!fs::exists(file_path)) {
        res.set_status(HTTPStatus::NotFound);
        res.set_text("File not found: " + url_path);
        return;
    }
    
    // Read file
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        res.set_status(HTTPStatus::InternalServerError);
        res.set_text("Failed to read file");
        return;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read file content
    std::vector<char> buffer(file_size);
    file.read(buffer.data(), file_size);
    
    // Determine content type
    std::string content_type = "application/octet-stream";
    if (url_path.ends_with(".html")) content_type = "text/html";
    else if (url_path.ends_with(".css")) content_type = "text/css";
    else if (url_path.ends_with(".js")) content_type = "application/javascript";
    else if (url_path.ends_with(".json")) content_type = "application/json";
    else if (url_path.ends_with(".png")) content_type = "image/png";
    else if (url_path.ends_with(".jpg") || url_path.ends_with(".jpeg")) content_type = "image/jpeg";
    else if (url_path.ends_with(".gif")) content_type = "image/gif";
    else if (url_path.ends_with(".svg")) content_type = "image/svg+xml";
    else if (url_path.ends_with(".mp4")) content_type = "video/mp4";
    else if (url_path.ends_with(".webm")) content_type = "video/webm";
    else if (url_path.ends_with(".mp3")) content_type = "audio/mpeg";
    else if (url_path.ends_with(".wav")) content_type = "audio/wav";
    
    // Set response
    res.set_status(HTTPStatus::OK);
    res.set_content_type(content_type);
    res.set_header("Content-Length", std::to_string(file_size));
    res.set_body(memory::ZeroCopyBuffer(buffer.data(), file_size));
}

// File upload handler
void handle_file_upload(HTTPRequest& req, HTTPResponse& res, const std::string& upload_path) {
    if (req.method() != HTTPMethod::POST) {
        res.set_status(HTTPStatus::MethodNotAllowed);
        res.set_text("Only POST method is allowed");
        return;
    }
    
    std::string filename = req.get_header("X-Filename", "upload.bin");
    std::string file_path = upload_path + "/" + filename;
    
    // Save file
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        res.set_status(HTTPStatus::InternalServerError);
        res.set_text("Failed to create file");
        return;
    }
    
    const auto& body = req.body();
    file.write(static_cast<const char*>(body.data()), body.size());
    file.close();
    
    res.set_status(HTTPStatus::OK);
    res.set_json(R"({"message": "File uploaded successfully", "filename": ")" + filename + R"("})");
}

int main() {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "Best_Server Web Server Daemon" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Create directories
    std::string base_path = "/data/data/com.termux/files/home/Server/web/static";
    std::string upload_path = "/data/data/com.termux/files/home/Server/web/uploads";
    std::string log_path = "/data/data/com.termux/files/home/Server/web/server.log";
    
    fs::create_directories(base_path);
    fs::create_directories(upload_path);
    
    // Create index.html
    std::string html_path = base_path + "/index.html";
    if (!fs::exists(html_path)) {
        std::ofstream html_file(html_path);
        if (html_file.is_open()) {
            html_file << "<!DOCTYPE html>\n<html><head><title>Best_Server</title></head>";
            html_file << "<body><h1>Best_Server is Running!</h1>";
            html_file << "<p>Server started successfully at " << std::chrono::system_clock::now().time_since_epoch().count() << "</p>";
            html_file << "<p>Features: File Upload/Download, Static Files, WebSocket</p></body></html>";
            html_file.close();
        }
    }
    
    // Create HTTP server
    HTTPServer server;
    
    // Configure server
    HTTPServerConfig config;
    config.address = "0.0.0.0";
    config.port = 8080;
    config.enable_http2 = true;
    config.enable_websocket = true;
    config.enable_compression = true;
    config.max_request_size = 100 * 1024 * 1024;
    
    // Route handlers
    server.get(".*", [base_path](HTTPRequest& req, HTTPResponse& res) {
        serve_static_file(req, res, base_path);
    });
    
    server.post("/upload", [upload_path](HTTPRequest& req, HTTPResponse& res) {
        handle_file_upload(req, res, upload_path);
    });
    
    server.get("/files/*", [base_path](HTTPRequest& req, HTTPResponse& res) {
        std::string filename = req.path().substr(7);
        std::string file_path = base_path + "/" + filename;
        
        if (!fs::exists(file_path)) {
            res.set_status(HTTPStatus::NotFound);
            res.set_text("File not found");
            return;
        }
        
        std::ifstream file(file_path, std::ios::binary);
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<char> buffer(file_size);
        file.read(buffer.data(), file_size);
        
        res.set_status(HTTPStatus::OK);
        res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        res.set_body(memory::ZeroCopyBuffer(buffer.data(), file_size));
    });
    
    server.get("/api/files", [upload_path](HTTPRequest& req, HTTPResponse& res) {
        (void)req;
        std::string json = "[";
        bool first = true;
        for (const auto& entry : fs::directory_iterator(upload_path)) {
            if (!first) json += ",";
            first = false;
            json += R"({"name": ")" + entry.path().filename().string() + R"(", "size": )" + 
                   std::to_string(fs::file_size(entry.path())) + "}";
        }
        json += "]";
        res.set_json(json);
    });
    
    server.get("/api/status", [](HTTPRequest& req, HTTPResponse& res) {
        (void)req;
        res.set_json(R"({"status": "running", "message": "Best_Server is active"})");
    });
    
    // Start server
    if (!server.start(config)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "\n✓ Server started successfully!" << std::endl;
    std::cout << "Address: http://" << config.address << ":" << config.port << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server..." << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Run until stopped
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    server.stop();
    
    std::cout << "\n✓ Server stopped" << std::endl;
    
    return 0;
}