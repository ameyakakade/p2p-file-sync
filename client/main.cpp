#include <iostream>
#include <thread>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#endif /* _WIN32 */

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080

#ifdef _WIN32
#define SOCK SOCKET
#define ERR_SOCK SOCKET_ERROR
#else
#define SOCK int
#define ERR_SOCK (-1)
#endif /* win32 */

void receive_messages(SOCK server_socket) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(server_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            std::cout << "\nServer disconnected.\n";
            break;
        }
        std::cout << "\nServer: " << buffer << "\nClient > " << std::flush;
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    SOCK client_socket = socket(AF_INET, SOCK_STREAM, 0);


    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == ERR_SOCK) {
        std::cout << "Connection failed.\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::string password;
    std::cout << "Enter Password: ";
    std::getline(std::cin, password);
    send(client_socket, password.c_str(), password.length(), 0);

    char auth_res[32] = { 0 };
    recv(client_socket, auth_res, sizeof(auth_res) - 1, 0);

    if (std::string(auth_res) != "AUTH_OK") {
        std::cout << "Authentication failed! Wrong password.\n";
#ifdef _WIN32
        closesocket(client_socket);
        WSACleanup();
#else
        close(client_socket);
#endif /* _WIN32 */
        return 0;
    }

    std::cout << "Connected & Authenticated!\n--- CHAT STARTED ---\n";

    std::thread rx_thread(receive_messages, client_socket);
    rx_thread.detach();

    std::string msg;
    while (true) {
        std::cout << "Client > ";
        std::getline(std::cin, msg);
        if (msg == "exit") break;
        send(client_socket, msg.c_str(), msg.length(), 0);
    }

#ifdef _WIN32
    closesocket(server_socket);
    WSACleanup();
#else
    close(client_socket);
#endif /* _WIN32 */
    return 0;
}
// test changes
