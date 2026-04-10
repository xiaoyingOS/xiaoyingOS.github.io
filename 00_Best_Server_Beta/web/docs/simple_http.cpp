// Simple HTTP Server with Event Loop - Fixed Version
// Supports unlimited file size

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <csignal>
#include <atomic>
#include <vector>
#include <chrono>
#include <sstream>

// Include monitoring module
#include "best_server/monitoring/monitoring.hpp"

namespace fs = std::filesystem;

std::atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

std::string get_content_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".gif")) return "image/gif";
    if (path.ends_with(".svg")) return "image/svg+xml";
    if (path.ends_with(".mp4")) return "video/mp4";
    if (path.ends_with(".webm")) return "video/webm";
    if (path.ends_with(".mp3")) return "audio/mpeg";
    if (path.ends_with(".wav")) return "audio/wav";
    if (path.ends_with(".zip")) return "application/zip";
    if (path.ends_with(".rar")) return "application/x-rar-compressed";
    if (path.ends_with(".7z")) return "application/x-7z-compressed";
    return "application/octet-stream";
}

std::string read_file_content(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return "";
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::string content;
    content.resize(size);
    file.read(&content[0], size);
    
    return content;
}

std::string build_http_response(const std::string& content, const std::string& content_type = "text/html") {
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + std::to_string(content.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += content;
    return response;
}

std::string build_http_error(int code, const std::string& message) {
    std::string response = "HTTP/1.1 " + std::to_string(code) + " Error\r\n";
    response += "Content-Type: text/html\r\n";
    response += "Content-Length: " + std::to_string(message.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += message;
    return response;
}

void handle_client(int client_fd, const std::string& base_path, const std::string& upload_path,
                   auto http_requests_total, auto http_requests_in_flight, 
                   auto file_uploads_total, auto file_downloads_total,
                   auto bytes_uploaded_total, auto bytes_downloaded_total,
                   auto request_duration_ms) {
    // Record request start
    auto request_start = std::chrono::high_resolution_clock::now();
    http_requests_total->increment();
    http_requests_in_flight->increment();
    
    // RAII guard for cleanup
    struct RequestGuard {
        std::chrono::high_resolution_clock::time_point start;
        decltype(request_duration_ms) duration_metric;
        decltype(http_requests_in_flight) in_flight_metric;
        bool active = true;
        
        ~RequestGuard() {
            if (active) {
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                duration_metric->observe(duration.count());
                in_flight_metric->decrement();
            }
        }
    };
    RequestGuard guard{request_start, request_duration_ms, http_requests_in_flight};
    
    // Read headers first
    char buffer[65536];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    std::string request(buffer);
    
    // Check if we have the complete headers (look for \r\n\r\n)
    size_t headers_end = request.find("\r\n\r\n");
    if (headers_end == std::string::npos) {
        // Need more data for headers
        std::string more;
        while ((bytes_read = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
            more.append(buffer, bytes_read);
            request += more;
            headers_end = request.find("\r\n\r\n");
            if (headers_end != std::string::npos) break;
        }
    }
    
    if (request.empty()) {
        close(client_fd);
        return;
    }
    
    // Parse HTTP request
    size_t pos = request.find(' ');
    if (pos == std::string::npos) {
        send(client_fd, build_http_error(400, "Bad Request").c_str(), 
             build_http_error(400, "Bad Request").size(), 0);
        close(client_fd);
        return;
    }
    
    std::string method = request.substr(0, pos);
    size_t pos2 = request.find(' ', pos + 1);
    if (pos2 == std::string::npos) {
        send(client_fd, build_http_error(400, "Bad Request").c_str(), 
             build_http_error(400, "Bad Request").size(), 0);
        close(client_fd);
        return;
    }
    
    std::string path = request.substr(pos + 1, pos2 - pos - 1);
    
    // Handle different paths
    if (path == "/") path = "/index.html";
    
    // Chunk upload endpoint
    if (path == "/api/upload/chunk" && method == "POST") {
        // Extract Content-Type and boundary
        size_t content_type_pos = request.find("Content-Type: ");
        if (content_type_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Missing Content-Type").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        size_t content_type_end = request.find("\r\n", content_type_pos);
        std::string content_type = request.substr(content_type_pos + 14, content_type_end - content_type_pos - 14);
        
        // Extract boundary
        size_t boundary_pos = content_type.find("boundary=");
        if (boundary_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Missing boundary").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        std::string boundary = content_type.substr(boundary_pos + 9);
        std::string boundary_marker = "--" + boundary;
        
        // Extract Content-Length
        size_t content_length_pos = request.find("Content-Length: ");
        size_t content_length = 0;
        if (content_length_pos != std::string::npos) {
            size_t content_length_end = request.find("\r\n", content_length_pos);
            std::string length_str = request.substr(content_length_pos + 16, content_length_end - content_length_pos - 16);
            content_length = std::stoull(length_str);
        }
        
        // Extract body (after headers)
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Invalid request format").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        // Read initial data to extract metadata (only first 64KB is enough for metadata)
        std::string initial_data = request.substr(body_pos + 4);
        size_t expected_body_size = content_length > 0 ? content_length : initial_data.size();
        
        // Ensure we have at least 64KB to parse metadata
        while (initial_data.size() < 65536 && initial_data.size() < expected_body_size) {
            ssize_t more_read = recv(client_fd, buffer, sizeof(buffer), 0);
            if (more_read <= 0) break;
            initial_data.append(buffer, more_read);
        }
        
        // Parse metadata from multipart/form-data
        std::string filename = "unknown";
        int chunk_index = 0;
        int total_chunks = 1;
        size_t total_size = 0;
        size_t file_data_start = 0;
        
        size_t pos = 0;
        bool found_file = false;
        
        while (pos < initial_data.size()) {
            // Find boundary
            size_t boundary_pos = initial_data.find(boundary_marker, pos);
            if (boundary_pos == std::string::npos) break;
            
            // Skip boundary
            pos = boundary_pos + boundary_marker.length();
            
            // Check for end boundary
            if (pos + 2 < initial_data.size() && initial_data.substr(pos, 2) == "--") {
                break;
            }
            
            // Skip \r\n after boundary
            if (pos + 2 < initial_data.size() && initial_data.substr(pos, 2) == "\r\n") {
                pos += 2;
            }
            
            // Find Content-Disposition header
            size_t cd_pos = initial_data.find("Content-Disposition:", pos);
            if (cd_pos == std::string::npos) break;
            
            size_t cd_end = initial_data.find("\r\n\r\n", cd_pos);
            if (cd_end == std::string::npos) break;
            
            std::string content_disposition = initial_data.substr(cd_pos, cd_end - cd_pos);
            
            // Extract field name
            size_t name_pos = content_disposition.find("name=\"");
            if (name_pos == std::string::npos) break;
            
            size_t name_start = name_pos + 6;
            size_t name_end = content_disposition.find("\"", name_start);
            if (name_end == std::string::npos) break;
            
            std::string field_name = content_disposition.substr(name_start, name_end - name_start);
            
            // Find data start (after \r\n\r\n)
            size_t data_start = cd_end + 4;
            
            // Find next boundary
            size_t next_boundary = initial_data.find("\r\n" + boundary_marker, data_start);
            if (next_boundary == std::string::npos) {
                // File data extends beyond initial_data, mark position
                if (field_name == "file") {
                    found_file = true;
                    file_data_start = data_start;
                    
                    // Extract filename
                    size_t filename_pos = content_disposition.find("filename=\"");
                    if (filename_pos != std::string::npos) {
                        size_t fn_start = filename_pos + 10;
                        size_t fn_end = content_disposition.find("\"", fn_start);
                        if (fn_end != std::string::npos) {
                            filename = content_disposition.substr(fn_start, fn_end - fn_start);
                        }
                    }
                }
                break;
            }
            
            std::string field_value = initial_data.substr(data_start, next_boundary - data_start);
            
            // Process field
            if (field_name == "chunkIndex") {
                chunk_index = std::stoi(field_value);
            } else if (field_name == "totalChunks") {
                total_chunks = std::stoi(field_value);
            } else if (field_name == "totalSize") {
                total_size = std::stoull(field_value);
            } else if (field_name == "file") {
                found_file = true;
                file_data_start = data_start;
                
                // Extract filename
                size_t filename_pos = content_disposition.find("filename=\"");
                if (filename_pos != std::string::npos) {
                    size_t fn_start = filename_pos + 10;
                    size_t fn_end = content_disposition.find("\"", fn_start);
                    if (fn_end != std::string::npos) {
                        filename = content_disposition.substr(fn_start, fn_end - fn_start);
                    }
                }
                
                // File data is found, break to stream it
                break;
            }
            
            pos = next_boundary + 2;
        }
        
        if (!found_file) {
            send(client_fd, build_http_error(400, "No file data found").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        // Save chunk to file with streaming
        std::string temp_dir = upload_path + "/.chunks";
        fs::create_directories(temp_dir);
        std::string chunk_filename = temp_dir + "/" + filename + ".chunk" + std::to_string(chunk_index);
            // DEBUG: Log chunk info
            std::cout << "Saving chunk: filename=" << filename << ", chunk_index=" << chunk_index << std::endl;
        std::ofstream chunk_file(chunk_filename, std::ios::binary);
        
        if (!chunk_file) {
            send(client_fd, build_http_error(500, "Failed to create chunk file").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        // Write initial file data
        size_t initial_file_end = initial_data.find("\r\n" + boundary_marker, file_data_start);
        if (initial_file_end != std::string::npos) {
            chunk_file.write(initial_data.data() + file_data_start, initial_file_end - file_data_start);
        }
        
        // Stream remaining data
        size_t bytes_written = initial_file_end != std::string::npos ? (initial_file_end - file_data_start) : 0;
        size_t bytes_read_total = initial_data.size();
        
        while (bytes_read_total < expected_body_size) {
            ssize_t more_read = recv(client_fd, buffer, sizeof(buffer), 0);
            if (more_read <= 0) break;
            
            bytes_read_total += more_read;
            
            // Check for boundary in the new data
            std::string new_data(buffer, more_read);
            size_t boundary_pos = new_data.find("\r\n" + boundary_marker);
            
            if (boundary_pos != std::string::npos) {
                // Write only up to boundary
                chunk_file.write(buffer, boundary_pos);
                bytes_written += boundary_pos;
                break;
            } else {
                chunk_file.write(buffer, more_read);
                bytes_written += more_read;
            }
        }
        
        chunk_file.flush();
        chunk_file.close();
        
        std::string json = "{\"message\": \"Chunk uploaded successfully\", \"filename\": \"" + filename + 
                          "\", \"chunkIndex\": " + std::to_string(chunk_index) + "}";
        std::string response = build_http_response(json, "application/json");
        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
        return;
    }
    
    // Complete chunk upload endpoint
    if (path == "/api/upload/complete" && method == "POST") {
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Invalid request").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        std::string body = request.substr(body_pos + 4);
        
        // Parse JSON
        size_t filename_pos = body.find("\"filename\":");
        if (filename_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Missing filename").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        size_t filename_start = body.find("\"", filename_pos + 12) + 1;
        size_t filename_end = body.find("\"", filename_start);
        std::string filename = body.substr(filename_start, filename_end - filename_start);
        
        size_t chunks_pos = body.find("\"totalChunks\":");
        int total_chunks = 1;
        if (chunks_pos != std::string::npos) {
            size_t chunks_start = chunks_pos + 14;
            size_t chunks_end = body.find(",", chunks_start);
            if (chunks_end == std::string::npos) chunks_end = body.find("}", chunks_start);
            total_chunks = std::stoi(body.substr(chunks_start, chunks_end - chunks_start));
        }
        
        // Create chunks directory and clean up any directories in it
        std::string temp_dir = upload_path + "/.chunks";
        fs::create_directories(temp_dir);
        
        // Clean up any directories in .chunks (shouldn't be there, but just in case)
        if (fs::exists(temp_dir) && fs::is_directory(temp_dir)) {
            for (const auto& entry : fs::directory_iterator(temp_dir)) {
                if (entry.is_directory()) {
                    // Remove directory (shouldn't exist)
                    fs::remove_all(entry.path());
                }
            }
        }
        
        // Check if file already exists
        std::string file_path = upload_path + "/" + filename;
        bool file_exists = fs::exists(file_path) && fs::is_regular_file(file_path);
        
        if (file_exists) {
            std::string json = "{\"message\": \"File already exists\", \"filename\": \"" + filename + 
                              "\", \"skipped\": true}";
            std::string response = build_http_response(json, "application/json");
            send(client_fd, response.c_str(), response.size(), 0);
            close(client_fd);
            return;
        }
        
        // Combine chunks
        std::ofstream output_file(file_path, std::ios::binary);
        
        if (!output_file) {
            send(client_fd, build_http_error(500, "Failed to create output file").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        size_t total_size = 0;
        int chunks_found = 0;
        
        for (int i = 0; i < total_chunks; i++) {
            std::string chunk_path = temp_dir + "/" + filename + ".chunk" + std::to_string(i);
            
            // Check if chunk exists and is a regular file
            if (fs::exists(chunk_path) && fs::is_regular_file(chunk_path)) {
                std::ifstream chunk_file(chunk_path, std::ios::binary);
                if (chunk_file) {
                    output_file << chunk_file.rdbuf();
                    try {
                        total_size += fs::file_size(chunk_path);
                    } catch (...) {
                        // If file_size fails, estimate from file position
                        chunk_file.seekg(0, std::ios::end);
                        total_size += chunk_file.tellg();
                    }
                    chunk_file.close();
                    fs::remove(chunk_path); // Delete chunk after combining
                    chunks_found++;
                }
            }
        }
        
        output_file.flush();
        output_file.close();
        
        // Check if all chunks were found
        if (chunks_found < total_chunks) {
            // Some chunks missing, delete incomplete file
            fs::remove(file_path);
            std::string error_json = R"({"message": "Missing chunks", "chunks_found": )" + std::to_string(chunks_found) + R"(, "total_chunks": )" + std::to_string(total_chunks) + R"(})";
            std::string response = build_http_error(400, error_json);
            send(client_fd, response.c_str(), response.size(), 0);
            close(client_fd);
            return;
        }
        
        // Record metrics
        file_uploads_total->increment();
        bytes_uploaded_total->increment(total_size);
        
        std::string json = "{\"message\": \"File uploaded successfully\", \"filename\": \"" + filename + 
                          "\", \"size\": " + std::to_string(total_size) + ", \"skipped\": false}";
        std::string response = build_http_response(json, "application/json");
        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
        return;
    }
    
    // File upload
    if (path == "/upload" && method == "POST") {
        // Extract Content-Type and boundary
        size_t content_type_pos = request.find("Content-Type: ");
        if (content_type_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Missing Content-Type").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        size_t content_type_end = request.find("\r\n", content_type_pos);
        std::string content_type = request.substr(content_type_pos + 14, content_type_end - content_type_pos - 14);
        
        // Check if multipart/form-data
        if (content_type.find("multipart/form-data") == std::string::npos) {
            send(client_fd, build_http_error(400, "Expected multipart/form-data").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        // Extract boundary
        size_t boundary_pos = content_type.find("boundary=");
        if (boundary_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Missing boundary").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        std::string boundary = content_type.substr(boundary_pos + 9);
        std::string boundary_marker = "--" + boundary;
        
        // Extract Content-Length
        size_t content_length_pos = request.find("Content-Length: ");
        size_t content_length = 0;
        if (content_length_pos != std::string::npos) {
            size_t content_length_end = request.find("\r\n", content_length_pos);
            std::string length_str = request.substr(content_length_pos + 16, content_length_end - content_length_pos - 16);
            content_length = std::stoull(length_str);
        }
        
        // Extract body (after headers) - Only read headers to find filename
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Invalid request format").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        // Extract only the first part to get filename
        std::string headers_only = request.substr(0, body_pos);
        
        // Parse multipart/form-data
        size_t first_boundary = headers_only.find(boundary_marker);
        if (first_boundary == std::string::npos) {
            send(client_fd, build_http_error(400, "Invalid multipart data").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        size_t part_start = first_boundary + boundary_marker.length();
        
        // Find Content-Disposition header
        size_t cd_pos = headers_only.find("Content-Disposition:", part_start);
        if (cd_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Missing Content-Disposition").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        size_t cd_end = headers_only.find("\r\n\r\n", cd_pos);
        if (cd_end == std::string::npos) {
            send(client_fd, build_http_error(400, "Invalid Content-Disposition").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        std::string content_disposition = headers_only.substr(cd_pos, cd_end - cd_pos);
        
        // Extract filename
        size_t filename_pos = content_disposition.find("filename=\"");
        if (filename_pos == std::string::npos) {
            send(client_fd, build_http_error(400, "Missing filename").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        size_t filename_start = filename_pos + 10;
        size_t filename_end = content_disposition.find("\"", filename_start);
        if (filename_end == std::string::npos) {
            send(client_fd, build_http_error(400, "Invalid filename").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        std::string filename = content_disposition.substr(filename_start, filename_end - filename_start);
        std::string file_path = upload_path + "/" + filename;
        
        // Check if file already exists and get expected size
        bool file_exists = fs::exists(file_path) && fs::is_regular_file(file_path);
        size_t existing_size = file_exists ? fs::file_size(file_path) : 0;
        
        // Stream the file content to disk
        std::ofstream file(file_path, std::ios::binary);
        if (!file) {
            send(client_fd, build_http_error(500, "Failed to create file").c_str(), 400, 0);
            close(client_fd);
            return;
        }
        
        // Calculate where the actual file content starts
        size_t content_start = body_pos + 4; // After \r\n\r\n
        size_t bytes_written = 0;
        
        // Write the initial data from the first recv
        if (bytes_read > content_start) {
            size_t initial_data_size = bytes_read - content_start;
            file.write(buffer + content_start, initial_data_size);
            bytes_written = initial_data_size;
        }
        
        // Stream remaining data
        while (bytes_written < content_length) {
            ssize_t more_read = recv(client_fd, buffer, sizeof(buffer), 0);
            if (more_read <= 0) break;
            
            // Check for boundary marker in the data
            std::string chunk(buffer, more_read);
            size_t boundary_pos = chunk.find("\r\n" + boundary_marker);
            
            if (boundary_pos != std::string::npos) {
                // Found the end boundary - write only up to it
                file.write(buffer, boundary_pos);
                bytes_written += boundary_pos;
                break;
            } else {
                // Write the entire chunk
                file.write(buffer, more_read);
                bytes_written += more_read;
            }
        }
        
        file.flush();
        file.close();
        
        // Check if file already existed with same size
        if (file_exists && existing_size == bytes_written) {
            // Same file - skip and return success
            std::string json = "{\"message\": \"File already exists (fast upload)\", \"filename\": \"" + filename + 
                              "\", \"size\": " + std::to_string(bytes_written) + ", \"skipped\": true}";
            std::string response = build_http_response(json, "application/json");
            send(client_fd, response.c_str(), response.size(), 0);
            close(client_fd);
            std::cout << "Fast upload (file exists): " << filename << " (" << bytes_written << " bytes)" << std::endl;
            return;
        }
        
        // Record upload metrics
        file_uploads_total->increment();
        bytes_uploaded_total->increment(bytes_written);
        
        // Send success response
        std::string json = "{\"message\": \"File uploaded successfully\", \"filename\": \"" + filename + 
                          "\", \"size\": " + std::to_string(bytes_written) + ", \"skipped\": false}";
        std::string response = build_http_response(json, "application/json");
        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
        std::cout << "Uploaded: " << filename << " (" << bytes_written << " bytes)" << std::endl;
        return;
    }
    
    // API endpoints
    if (path == "/api/files" && method == "GET") {
        std::string json = "[";
        bool first = true;
        for (const auto& entry : fs::directory_iterator(upload_path)) {
                       if (!fs::is_regular_file(entry.path())) continue;
 if (!first) json += ",";
            first = false;
            json += R"({"name": ")" + entry.path().filename().string() + R"(", "size": )" + 
                   std::to_string(fs::file_size(entry.path())) + "}";
        }
        json += "]";
        std::string response = build_http_response(json, "application/json");
        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
        return;
    }
    
    // Delete file endpoint
    if (path == "/api/delete" && method == "POST") {
        size_t content_length_pos = request.find("Content-Length: ");
        size_t content_length = 0;
        if (content_length_pos != std::string::npos) {
            size_t content_length_end = request.find("\r\n", content_length_pos);
            std::string length_str = request.substr(content_length_pos + 16, content_length_end - content_length_pos - 16);
            content_length = std::stoull(length_str);
        }
        
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos != std::string::npos) {
            std::string body = request.substr(body_pos + 4);
            
            // Read more data if needed
            size_t expected_body_size = content_length > 0 ? content_length : body.size();
            while (body.size() < expected_body_size) {
                ssize_t more_read = recv(client_fd, buffer, sizeof(buffer), 0);
                if (more_read <= 0) break;
                body.append(buffer, more_read);
            }
            
            
            // Extract filename from JSON
            size_t filename_pos = body.find("\"filename\":");
            if (filename_pos != std::string::npos) {
                // Find the opening quote after "filename":
                size_t filename_start = body.find("\"", filename_pos + 11); // After "filename":
                if (filename_start != std::string::npos) {
                    size_t filename_end = body.find("\"", filename_start + 1);
                    if (filename_end != std::string::npos) {
                        std::string filename = body.substr(filename_start + 1, filename_end - filename_start - 1);
                        std::string file_path = upload_path + "/" + filename;
                        
                        
                        if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
                            if (fs::remove(file_path)) {
                                std::string json = R"({"message": "File deleted successfully", "filename": ")" + filename + R"("})";
                                std::string response = build_http_response(json, "application/json");
                                send(client_fd, response.c_str(), response.size(), 0);
                                close(client_fd);
                                return;
                            }
                        }
                        
                        std::string json = R"({"message": "Failed to delete file", "filename": ")" + filename + R"("})";
                        std::string response = build_http_error(404, json);
                        send(client_fd, response.c_str(), response.size(), 0);
                        close(client_fd);
                        return;
                    }
                }
            }
        }
        
        std::string json = R"({"message": "Invalid request - missing filename"})";
        std::string response = build_http_error(400, json);
        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
        return;
    }
    
    if (path == "/api/status") {
        std::string json = R"({"status": "running", "message": "Simple HTTP Server is active", "features": ["unlimited_file_upload", "streaming", "file_deletion", "monitoring"]})";
        std::string response = build_http_response(json, "application/json");
        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
        return;
    }
    
    // Metrics API - return current metrics in JSON format
    if (path == "/api/metrics") {
        auto metrics = best_server::monitoring::Monitoring::metrics()->get_all_metrics();
        std::ostringstream json;
        json << "{";
        bool first = true;
        for (const auto& metric : metrics) {
            if (!first) json << ",";
            first = false;
            json << "\"" << metric->name() << "\": {";
            json << "\"type\": ";
            switch (metric->type()) {
                case best_server::monitoring::MetricType::COUNTER:
                    json << "\"counter\"";
                    break;
                case best_server::monitoring::MetricType::GAUGE:
                    json << "\"gauge\"";
                    break;
                case best_server::monitoring::MetricType::HISTOGRAM:
                    json << "\"histogram\"";
                    break;
                default:
                    json << "\"unknown\"";
            }
            auto value = metric->get();
            json << ", \"value\": ";
            if (std::holds_alternative<double>(value)) {
                json << std::get<double>(value);
            } else if (std::holds_alternative<int64_t>(value)) {
                json << std::get<int64_t>(value);
            }
            json << "}";
        }
        json << "}";
        std::string response = build_http_response(json.str(), "application/json");
        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
        return;
    }
    
    // Performance API - return performance statistics
    if (path == "/api/performance") {
        auto perf = best_server::monitoring::Monitoring::performance();
        auto ops = perf->get_operations();
        std::ostringstream json;
        json << "{";
        json << "\"operations\": {";
        bool first_op = true;
        for (const auto& [op_name, durations] : ops) {
            if (!first_op) json << ",";
            first_op = false;
            json << "\"" << op_name << "\": {";
            json << "\"avg_ms\": " << perf->get_average_duration(op_name).count();
            json << ", \"p95_ms\": " << perf->get_p95_duration(op_name).count();
            json << ", \"p99_ms\": " << perf->get_p99_duration(op_name).count();
            json << ", \"count\": " << durations.size();
            json << "}";
        }
        json << "}}";
        std::string response = build_http_response(json.str(), "application/json");
        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
        return;
    }
    
    // File download from uploads
    if (path.starts_with("/download/")) {
        std::string filename = path.substr(10);
        std::string file_path = upload_path + "/" + filename;
        
        if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
            std::string content = read_file_content(file_path);
            
            // Record download metrics
            file_downloads_total->increment();
            bytes_downloaded_total->increment(content.size());
            
            std::string response = build_http_response(content, "application/octet-stream");
            send(client_fd, response.c_str(), response.size(), 0);
            close(client_fd);
            return;
        }
    }
    
    // Static files
    if (path.starts_with("/files/")) {
        std::string filename = path.substr(7);
        std::string file_path = base_path + "/" + filename;
        
        if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
            std::string content = read_file_content(file_path);
            std::string response = build_http_response(content, get_content_type(filename));
            send(client_fd, response.c_str(), response.size(), 0);
            close(client_fd);
            return;
        }
    }
    
    // Static files
    std::string file_path = base_path + path;
    
    // Security check
    if (file_path.find("..") != std::string::npos) {
        send(client_fd, build_http_error(403, "Forbidden").c_str(), 
             build_http_error(403, "Forbidden").size(), 0);
        close(client_fd);
        return;
    }
    
    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
        std::string content = read_file_content(file_path);
        std::string response = build_http_response(content, get_content_type(path));
        send(client_fd, response.c_str(), response.size(), 0);
    } else {
        std::string not_found = "<html><body><h1>404 Not Found</h1><p>File not found: " + path + "</p></body></html>";
        std::string response = build_http_error(404, not_found);
        send(client_fd, response.c_str(), response.size(), 0);
    }
    
    close(client_fd);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "Simple HTTP Server (Fixed)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Create directories
    std::string base_path = "/data/data/com.termux/files/home/Server/web/static";
    std::string upload_path = "/data/data/com.termux/files/home/Server/web/uploads";
    
    fs::create_directories(base_path);
    fs::create_directories(upload_path);
    
    // Create index.html
    std::string html_path = base_path + "/index.html";
    if (!fs::exists(html_path)) {
        std::ofstream html_file(html_path);
        if (html_file.is_open()) {
            html_file << "<!DOCTYPE html>\n<html><head><title>Simple HTTP Server</title>";
            html_file << "<style>body{font-family:Arial;max-width:800px;margin:50px auto;padding:20px;}h1{color:#333;}";
            html_file << ".section{margin:30px 0;padding:20px;border:1px solid #ddd;border-radius:5px;}";
            html_file << "button{padding:10px 20px;margin:5px;cursor:pointer;}input{padding:10px;margin:5px;}</style></head>";
            html_file << "<body><h1>Simple HTTP Server</h1>";
            html_file << "<div class='section'><h2>File Upload (Unlimited Size)</h2>";
            html_file << "<input type='file' id='fileInput'>";
            html_file << "<button onclick='uploadFile()'>Upload</button>";
            html_file << "<p id='uploadStatus'></p></div>";
            html_file << "<div class='section'><h2>File List</h2>";
            html_file << "<button onclick='listFiles()'>Refresh</button>";
            html_file << "<div id='fileList'></div></div>";
            html_file << "<script>";
            html_file << "function uploadFile(){var f=document.getElementById('fileInput').files[0];";
            html_file << "if(!f){alert('Select a file');return;}";
            html_file << "var xhr=new XMLHttpRequest();xhr.open('POST','/upload');";
            html_file << "xhr.setRequestHeader('X-Filename',f.name);";
            html_file << "xhr.onload=function(){document.getElementById('uploadStatus').textContent='Uploaded: '+xhr.responseText;};";
            html_file << "xhr.send(f);}";
            html_file << "function listFiles(){var xhr=new XMLHttpRequest();";
            html_file << "xhr.open('GET','/api/files');xhr.onload=function(){";
            html_file << "var files=JSON.parse(xhr.responseText);var html='';";
            html_file << "files.forEach(f=>{html+='<p>'+f.name+' ('+(f.size/1024/1024).toFixed(2)+' MB)</p>';});";
            html_file << "document.getElementById('fileList').innerHTML=html;};";
            html_file << "xhr.send();}";
            html_file << "listFiles();</script></body></html>";
            html_file.close();
            std::cout << "Created index.html" << std::endl;
        }
    }
    
    // Initialize monitoring
    best_server::monitoring::Monitoring::initialize("web_server");
    
    // Create metrics
    auto http_requests_total = best_server::monitoring::Monitoring::metrics()->counter(
        "http_requests_total", "Total number of HTTP requests");
    auto http_requests_in_flight = best_server::monitoring::Monitoring::metrics()->gauge(
        "http_requests_in_flight", "Number of HTTP requests currently being processed");
    auto file_uploads_total = best_server::monitoring::Monitoring::metrics()->counter(
        "file_uploads_total", "Total number of file uploads");
    auto file_downloads_total = best_server::monitoring::Monitoring::metrics()->counter(
        "file_downloads_total", "Total number of file downloads");
    auto bytes_uploaded_total = best_server::monitoring::Monitoring::metrics()->counter(
        "bytes_uploaded_total", "Total bytes uploaded");
    auto bytes_downloaded_total = best_server::monitoring::Monitoring::metrics()->counter(
        "bytes_downloaded_total", "Total bytes downloaded");
    auto request_duration_ms = best_server::monitoring::Monitoring::metrics()->histogram(
        "request_duration_ms", {1, 5, 10, 50, 100, 500, 1000, 5000}, "HTTP request duration in milliseconds");
    
    std::cout << "✓ Monitoring initialized" << std::endl;
    
    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd < 0) {
        std::cerr << "ERROR: socket() failed" << std::endl;
        return 1;
    }
    
    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "ERROR: bind() failed" << std::endl;
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 128) < 0) {
        std::cerr << "ERROR: listen() failed" << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cout << "\n✓ Server started successfully!" << std::endl;
    std::cout << "Address: http://0.0.0.0:8080" << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "\nFeatures:" << std::endl;
    std::cout << "  ✓ Unlimited file upload size" << std::endl;
    std::cout << "  ✓ Unlimited file download size" << std::endl;
    std::cout << "  ✓ Streaming support" << std::endl;
    std::cout << "\nOpen http://localhost:8080 in your browser" << std::endl;
    std::cout << "Press Ctrl+C to stop the server..." << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Main loop
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            std::cerr << "ERROR: accept() failed: " << strerror(errno) << std::endl;
            continue;
        }
        
        // Handle client in a separate thread
        std::thread([client_fd, base_path, upload_path, http_requests_total, http_requests_in_flight,
                     file_uploads_total, file_downloads_total, bytes_uploaded_total, bytes_downloaded_total,
                     request_duration_ms]() {
            handle_client(client_fd, base_path, upload_path, http_requests_total, http_requests_in_flight,
                         file_uploads_total, file_downloads_total, bytes_uploaded_total, bytes_downloaded_total,
                         request_duration_ms);
        }).detach();
    }
    
    std::cout << "\nStopping server..." << std::endl;
    close(server_fd);
    std::cout << "✓ Server stopped" << std::endl;
    
    return 0;
}