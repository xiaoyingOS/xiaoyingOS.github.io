#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>

#define PORT 8080
#define BUFFER_SIZE 65536
#define UPLOAD_PATH "/data/data/com.termux/files/home/Server/web/uploads"

void handle_upload(int client_fd, char *request, size_t request_len) {
    char *filename_start = strstr(request, "X-Filename:");
    if (!filename_start) {
        const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 20\r\n\r\nMissing X-Filename";
        send(client_fd, response, strlen(response), 0);
        return;
    }
    
    filename_start += 12;
    char *filename_end = strstr(filename_start, "\r\n");
    if (!filename_end) {
        const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 20\r\n\r\nInvalid filename";
        send(client_fd, response, strlen(response), 0);
        return;
    }
    
    char filename[256];
    size_t filename_len = filename_end - filename_start;
    if (filename_len >= sizeof(filename)) filename_len = sizeof(filename) - 1;
    strncpy(filename, filename_start, filename_len);
    filename[filename_len] = '\0';
    
    char *content_length_start = strstr(request, "Content-Length:");
    if (!content_length_start) {
        const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 22\r\n\r\nMissing Content-Length";
        send(client_fd, response, strlen(response), 0);
        return;
    }
    
    content_length_start += 16;
    int content_length = atoi(content_length_start);
    
    char *body_start = strstr(request, "\r\n\r\n");
    if (!body_start) {
        const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\nInvalid request";
        send(client_fd, response, strlen(response), 0);
        return;
    }
    
    body_start += 4;
    size_t body_len = request_len - (body_start - request);
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", UPLOAD_PATH, filename);
    
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        const char *response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 18\r\n\r\nFailed to create file";
        send(client_fd, response, strlen(response), 0);
        return;
    }
    
    if (body_len > 0) {
        write(fd, body_start, body_len);
    }
    
    int remaining = content_length - body_len;
    if (remaining > 0) {
        char buffer[BUFFER_SIZE];
        while (remaining > 0) {
            ssize_t bytes_read = recv(client_fd, buffer, (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE, 0);
            if (bytes_read <= 0) break;
            write(fd, buffer, bytes_read);
            remaining -= bytes_read;
        }
    }
    
    close(fd);
    
    char response[512];
    snprintf(response, sizeof(response), 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{\"message\": \"File uploaded successfully\", \"filename\": \"%s\"}", filename);
    send(client_fd, response, strlen(response), 0);
    
    printf("Uploaded: %s\n", filename);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    mkdir(UPLOAD_PATH, 0755);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    
    printf("Upload Test Server running on port %d\n", PORT);
    printf("Upload path: %s\n", UPLOAD_PATH);
    fflush(stdout);
    
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            continue;
        }
        
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            
            if (strncmp(buffer, "POST /upload ", 12) == 0) {
                handle_upload(client_fd, buffer, bytes_read);
            } else {
                const char *response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "<html><body><h1>Upload Test Server</h1>"
                    "<p>POST /upload - Upload file with X-Filename header</p>"
                    "<p>Example: curl -X POST http://localhost:8080/upload -H \"X-Filename: test.txt\" --data-binary @test.txt</p>"
                    "</body></html>";
                send(client_fd, response, strlen(response), 0);
            }
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    return 0;
}