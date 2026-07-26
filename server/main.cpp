#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define PASSWORD "secret123"

void receive_messages(SOCKET client_socket) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            std::cout << "\nClient disconnected.\n";
            break;
        }
        std::cout << "\nClient: " << buffer << "\nServer > " << std::flush;
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, 1);

    std::cout << "Server listening on port " << PORT << "...\n";

    sockaddr_in client_addr;
    int client_size = sizeof(client_addr);
    SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_size);

    std::cout << "Client connected! Verifying password...\n";

    // Password Check
    char pass_buf[1024] = { 0 };
    recv(client_socket, pass_buf, sizeof(pass_buf) - 1, 0);

    if (std::string(pass_buf) != PASSWORD) {
        std::cout << "Incorrect password attempt. Closing connection.\n";
        send(client_socket, "AUTH_FAILED", 11, 0);
        closesocket(client_socket);
        closesocket(server_socket);
        WSACleanup();
        return 0;
    }

    send(client_socket, "AUTH_OK", 7, 0);
    std::cout << "Client authenticated successfully!\n--- CHAT STARTED ---\n";

    std::thread rx_thread(receive_messages, client_socket);
    rx_thread.detach();

    std::string msg;
    while (true) {
        std::cout << "Server > ";
        std::getline(std::cin, msg);
        if (msg == "exit") break;
        send(client_socket, msg.c_str(), msg.length(), 0);
    }

    closesocket(client_socket);
    closesocket(server_socket);
    WSACleanup();
    return 0;
}