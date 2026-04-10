#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to connect to server" << std::endl;
        close(sock);
        return 1;
    }

    std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    send(sock, request.c_str(), request.length(), 0);

    char buffer[4096];
    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        std::cout << "Response received (" << bytes_received << " bytes):" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << buffer << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    } else {
        std::cerr << "Failed to receive response" << std::endl;
    }

    close(sock);
    return 0;
}
