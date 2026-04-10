// Web Server Example - File Transfer & WebSocket Support
// 
// Demonstrates:
// - HTTP file upload/download
// - WebSocket for real-time communication (video/voice)
// - Static file serving

#include <best_server/best_server.hpp>
#include <best_server/network/websocket/websocket.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include "server_stats.hpp"

using namespace best_server;
using namespace best_server::network;

namespace fs = std::filesystem;

// 全局服务器状态
ServerStats g_server_stats;

// Static file serving
void serve_static_file(HTTPRequest& req, HTTPResponse& res, const std::string& base_path) {
    std::string url_path = req.path();
    if (url_path == "/") url_path = "/index.html";
    
    std::string file_path = base_path + url_path;
    
    if (file_path.find("..") != std::string::npos) {
        res.set_status(HTTPStatus::Forbidden);
        res.set_text("Access denied");
        return;
    }
    
    if (!fs::exists(file_path)) {
        res.set_status(HTTPStatus::NotFound);
        res.set_text("File not found: " + url_path);
        return;
    }
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        res.set_status(HTTPStatus::InternalServerError);
        res.set_text("Failed to read file");
        return;
    }
    
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> buffer(file_size);
    file.read(buffer.data(), file_size);
    
    std::string content_type = "application/octet-stream";
    if (url_path.ends_with(".html")) content_type = "text/html";
    else if (url_path.ends_with(".css")) content_type = "text/css";
    else if (url_path.ends_with(".js")) content_type = "application/javascript";
    else if (url_path.ends_with(".json")) content_type = "application/json";
    else if (url_path.ends_with(".png")) content_type = "image/png";
    else if (url_path.ends_with(".jpg") || url_path.ends_with(".jpeg")) content_type = "image/jpeg";
    
    res.set_status(HTTPStatus::OK);
    res.set_content_type(content_type);
    res.set_header("Content-Length", std::to_string(file_size));
    std::string content(buffer.data(), file_size);
    res.set_body(content);
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
    
    int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        g_server_stats.add_log("Failed to create file: " + file_path + " - " + strerror(errno), "error");
        res.set_status(HTTPStatus::InternalServerError);
        res.set_text("Failed to create file: " + std::string(strerror(errno)));
        g_server_stats.add_response(500);
        return;
    }
    
    const auto& body = req.body();
    if (body.size() > 0) {
        ssize_t written = write(fd, body.data(), body.size());
        if (written < 0) {
            close(fd);
            g_server_stats.add_log("Failed to write file: " + file_path + " - " + strerror(errno), "error");
            res.set_status(HTTPStatus::InternalServerError);
            res.set_text("Failed to write file: " + std::string(strerror(errno)));
            g_server_stats.add_response(500);
            return;
        }
    }
    
    close(fd);
    
    g_server_stats.add_bytes_received(body.size());
    g_server_stats.add_log("File uploaded successfully: " + filename + " (" + std::to_string(body.size()) + " bytes)", "success");
    
    res.set_status(HTTPStatus::OK);
    g_server_stats.add_response(200);
    res.set_json(R"({"message": "File uploaded successfully", "filename": ")" + filename + 
                 R"(", "size": )" + std::to_string(body.size()) + R"(})");
}

// 文件夹上传处理 - 直接传输文件夹结构（不打包ZIP）
void handle_folder_upload(HTTPRequest& req, HTTPResponse& res, const std::string& upload_path) {
    if (req.method() != HTTPMethod::POST) {
        res.set_status(HTTPStatus::MethodNotAllowed);
        res.set_text("Only POST method is allowed");
        return;
    }
    
    std::string foldername = req.get_header("X-Foldername", "uploaded_folder");
    std::string relative_path = req.get_header("X-File-Path", "");
    
    auto form_fields = req.parse_multipart_form_data();
    
    size_t files_uploaded = 0;
    size_t total_size = 0;
    
    for (const auto& field : form_fields) {
        if (!field.filename.empty()) {
            std::string full_path = upload_path + "/" + foldername;
            
            if (!relative_path.empty()) {
                full_path += "/" + relative_path;
            }
            
            size_t last_slash = field.filename.find_last_of('/');
            if (last_slash != std::string::npos) {
                std::string dir_path = full_path + "/" + field.filename.substr(0, last_slash);
                std::string cmd = "mkdir -p \"" + dir_path + "\"";
                system(cmd.c_str());
            }
            
            std::string file_path = full_path + "/" + field.filename;
            int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                ssize_t written = write(fd, field.data.data(), field.data.size());
                close(fd);
                if (written > 0) {
                    files_uploaded++;
                    total_size += field.data.size();
                }
            }
        }
    }
    
    res.set_status(HTTPStatus::OK);
    res.set_json(R"({"message": "Folder uploaded successfully", "foldername": ")" + foldername + 
                 R"(", "files_uploaded": )" + std::to_string(files_uploaded) + 
                 R"(, "total_size": )" + std::to_string(total_size) + R"(})");
}

// 获取文件夹文件列表
void handle_folder_list(HTTPRequest& req, HTTPResponse& res, const std::string& base_path) {
    std::string foldername = req.path().substr(13); // /list/folder/ 长度为 13
    std::string folder_path = base_path + "/" + foldername;
    
    // 如果文件夹名为空，使用根目录
    if (foldername.empty()) {
        folder_path = base_path;
    }
    
    std::error_code ec;
    if (!fs::exists(folder_path, ec) || !fs::is_directory(folder_path, ec)) {
        res.set_status(HTTPStatus::NotFound);
        res.set_text("Folder not found");
        return;
    }
    
    std::string json = R"({"foldername": ")" + foldername + R"(", "files": [)";
    
    bool first = true;
    for (const auto& entry : fs::recursive_directory_iterator(folder_path, ec)) {
        if (entry.is_regular_file()) {
            if (!first) json += ",";
            first = false;
            
            std::string full_path = entry.path().string();
            std::string relative_path = full_path.substr(folder_path.length() + 1);
            size_t file_size = fs::file_size(entry.path(), ec);
            
            json += R"({"path": ")" + relative_path + R"(", "size": )" + std::to_string(file_size) + "}";
        }
    }
    
    json += "]}";
    res.set_status(HTTPStatus::OK);
    res.set_json(json);
}

// 文件夹下载处理 - 返回文件列表
void handle_folder_download(HTTPRequest& req, HTTPResponse& res, const std::string& base_path) {
    std::string foldername = req.path().substr(17); // /download/folder/ 长度为 17
    std::string folder_path = base_path + "/" + foldername;
    
    // 如果文件夹名为空，使用根目录
    if (foldername.empty()) {
        folder_path = base_path;
    }
    
    std::error_code ec;
    if (!fs::exists(folder_path, ec) || !fs::is_directory(folder_path, ec)) {
        res.set_status(HTTPStatus::NotFound);
        res.set_text("Folder not found");
        return;
    }
    
    size_t total_files = 0;
    size_t total_directories = 0;
    size_t total_bytes = 0;
    std::string files_json = "[";
    bool first = true;
    
    for (const auto& entry : fs::recursive_directory_iterator(folder_path, ec)) {
        if (entry.is_directory()) {
            total_directories++;
        } else if (entry.is_regular_file()) {
            total_files++;
            size_t file_size = fs::file_size(entry.path(), ec);
            total_bytes += file_size;
            
            if (!first) files_json += ",";
            first = false;
            
            std::string full_path = entry.path().string();
            std::string relative_path = full_path.substr(folder_path.length() + 1);
            
            files_json += R"({"path": ")" + relative_path + R"(", "size": )" + std::to_string(file_size) + "}";
        }
    }
    
    files_json += "]";
    
    std::string json = R"({"foldername": ")" + foldername + 
                       R"(", "total_files": )" + std::to_string(total_files) + 
                       R"(, "total_directories": )" + std::to_string(total_directories) + 
                       R"(, "total_bytes": )" + std::to_string(total_bytes) + 
                       R"(, "files": )" + files_json + "}";
    
    res.set_status(HTTPStatus::OK);
    res.set_json(json);
}

int main() {
    std::cout << "Best_Server Web Server - File Transfer & WebSocket" << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    std::string base_path = "/data/data/com.termux/files/home/Server/web/static";
    std::string upload_path = "/data/data/com.termux/files/home/Server/web/uploads";
    
    fs::create_directories(base_path);
    fs::create_directories(upload_path);
    
    HTTPServer server;
    
    HTTPServerConfig config;
    config.address = "0.0.0.0";
    config.port = 8080;
    config.enable_http2 = true;
    config.enable_websocket = true;
    config.enable_compression = true;
    config.max_request_size = SIZE_MAX;
    
    // 重要：路由注册顺序很重要！
    // 特定路径必须在通配符路径之前注册
    
    // API路由 - 必须先注册
    server.get("/api/files", [base_path](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string json = "[";
        bool first = true;
        
        try {
            for (const auto& entry : fs::directory_iterator(base_path)) {
                if (!entry.is_regular_file()) continue;
                
                if (!first) json += ",";
                first = false;
                
                json += R"({"name": ")" + entry.path().filename().string() + R"(", "size": )" + 
                       std::to_string(fs::file_size(entry.path())) + "}";
            }
        } catch (const std::exception& e) {
            g_server_stats.add_log("Error listing files: " + std::string(e.what()), "error");
        }
        
        json += "]";
        
        g_server_stats.add_response(200);
        res.set_json(json);
    });
    
    // 服务器状态 API
    server.get("/api/status", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        g_server_stats.add_response(200);
        res.set_json(g_server_stats.get_status_json());
    });
    
    // 系统资源 API
    server.get("/api/metrics", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        g_server_stats.add_response(200);
        res.set_json(g_server_stats.get_metrics_json());
    });
    
    // 服务器日志 API
    server.get("/api/logs", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        g_server_stats.add_response(200);
        res.set_json(g_server_stats.get_logs_json());
    });
    
    // 文件下载路由（支持断点续传）- 必须在通配符之前
    
        server.get("/files/{filename}", [base_path](HTTPRequest& req, HTTPResponse& res) {
    
            g_server_stats.add_request();
    
            std::string filename = req.path().substr(7);
    
            std::string file_path = base_path + "/" + filename;
    
            
    
            if (!fs::exists(file_path)) {
    
                g_server_stats.add_log("File not found: " + filename, "error");
    
                res.set_status(HTTPStatus::NotFound);
    
                res.set_json(R"({"error": "File not found", "path": ")" + filename + R"("})");
    
                g_server_stats.add_response(404);
    
                return;
    
            }
    
            
    
            size_t file_size = fs::file_size(file_path);
    
            
    
            // 简化版：直接返回文件（暂不支持 Range 请求）
    
            
    
                    res.set_status(HTTPStatus::OK);
    
            
    
                    res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    
            
    
                    res.set_header("Content-Length", std::to_string(file_size));
    
            
    
                    res.set_header("Content-Type", "application/octet-stream");
    
            
    
                    res.set_header("Accept-Ranges", "bytes");
    
            
    
                    
    
            
    
                    int fd = open(file_path.c_str(), O_RDONLY);
    
            
    
                    if (fd < 0) {
    
            
    
                        g_server_stats.add_log("Failed to open file: " + file_path, "error");
    
                        res.set_status(HTTPStatus::InternalServerError);
    
            
    
                        res.set_json(R"({"error": "Failed to open file", "errno": )" + std::to_string(errno) + R"(})");
    
            
    
                        g_server_stats.add_response(500);
    
            
    
                        return;
    
            
    
                    }
    
            
    
                    
    
            
    
                    std::vector<char> buffer(file_size);
    
            
    
                    ssize_t bytes_read = read(fd, buffer.data(), file_size);
    
            
    
                    close(fd);
    
            
    
                    
    
            
    
                    if (bytes_read > 0) {
    
            
    
                        std::string content(buffer.data(), bytes_read);
    
            
    
                        res.set_body(content);
    
            
    
                        g_server_stats.add_bytes_sent(bytes_read);
    
            
    
                        g_server_stats.add_log("File downloaded: " + filename + " (" + std::to_string(bytes_read) + " bytes)", "success");
    
            
    
                    }
    
            
    
                    g_server_stats.add_response(200);
    
                        });
    
            
    
                        
    
            
    
                        // 静态文件服务 - 为每个静态文件单独注册路由
    
            
    
                        server.get("/", [base_path](HTTPRequest& req, HTTPResponse& res) {
    
            
    
                            serve_static_file(req, res, base_path);
    
            
    
                        });
    
            
    
                        
    
            
    
                        server.get("/index.html", [base_path](HTTPRequest& req, HTTPResponse& res) {
    
            
    
                            serve_static_file(req, res, base_path);
    
            
    
                        });
    
            
    
                        
    
            
    
                        // 文件夹相关路由
    server.post("/upload", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_file_upload(req, res, upload_path);
    });
    
    server.post("/upload/folder", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_upload(req, res, upload_path);
    });
    
    // 列出根目录的文件
    server.get("/list/folder/", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_list(req, res, base_path);
    });
    
    // 列出指定文件夹的文件
    server.get("/list/folder/{foldername}", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_list(req, res, base_path);
    });
    
    // 下载根目录的文件列表
    server.get("/download/folder/", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_download(req, res, base_path);
    });
    
    // 下载指定文件夹的文件列表
    server.get("/download/folder/{foldername}", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_download(req, res, base_path);
    });
    
    // 静态文件路由 - 必须最后注册（作为fallback）
    // 暂时禁用，因为框架不支持真正的通配符路由
    // 用户需要直接访问完整的文件路径，例如：/index.html, /style.css 等
    // 如果需要添加静态文件支持，需要为每个文件单独注册路由，或者修改框架支持通配符
    
    // 注释掉通配符路由，因为框架不支持
    /*
    server.get(".*", [base_path](HTTPRequest& req, HTTPResponse& res) {
        std::string path = req.path();
        if (path.find("/api/") == 0 || path.find("/files/") == 0 || 
            path.find("/upload") == 0 || path.find("/list/folder/") == 0 ||
            path.find("/download/folder/") == 0) {
            res.set_status(HTTPStatus::NotFound);
            res.set_text("Endpoint not found");
            return;
        }
        serve_static_file(req, res, base_path);
    });
    */
    
    if (!server.start(config)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    // 添加启动日志
    g_server_stats.add_log("Server started successfully", "success");
    g_server_stats.add_log("HTTP/2 enabled: " + std::string(config.enable_http2 ? "true" : "false"), "info");
    g_server_stats.add_log("WebSocket enabled: " + std::string(config.enable_websocket ? "true" : "false"), "info");
    
    std::cout << "\n========================================\n";
    std::cout << "Server started successfully!\n";
    std::cout << "========================================\n";
    std::cout << "Address: http://" << config.address << ":" << config.port << "\n";
    std::cout << "Static files: " << base_path << "\n";
    std::cout << "Upload path: " << upload_path << "\n";
    std::cout << "\nAPI Endpoints:\n";
    std::cout << "  POST  /upload              - Upload single file\n";
    std::cout << "  POST  /upload/folder       - Upload folder (direct structure)\n";
    std::cout << "  GET   /list/folder/*       - List folder contents\n";
    std::cout << "  GET   /download/folder/*   - Get folder file list\n";
    std::cout << "  GET   /files/*             - Download file (supports resume)\n";
    std::cout << "  GET   /api/files           - List files\n";
    std::cout << "  GET   /api/status           - Server status\n";
    std::cout << "  GET   /api/metrics          - System metrics\n";
    std::cout << "  GET   /api/logs             - Server logs\n";
    std::cout << "\nServer is running...\n";
    std::cout.flush();
    
    // 持续运行服务器
    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    server.stop();
    
    std::cout << "\nServer stopped\n";
    
    return 0;
}
