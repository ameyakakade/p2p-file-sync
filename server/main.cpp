#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

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
#define PASSWORD "secret123"

using namespace std;

#ifdef _WIN32
#define SOCK SOCKET
#define INV_SOCK INVALID_SOCKET
#else
#define SOCK int
#define INV_SOCK (-1)
#endif /* win32 */

vector<SOCK> clients;
mutex clients_mutex;

void sendMessage(const string& msg, SOCK sender) {
    lock_guard<mutex> lock(clients_mutex);
    for (SOCK client : clients) {
        if (client != sender) {
            send(client, msg.c_str(), msg.length(), 0);
        }
    }
}

void removeClient(SOCK client) {
    lock_guard<mutex> lock(clients_mutex);
    clients.erase(remove(clients.begin(), clients.end(), client), clients.end());
#ifdef _WIN32
    closesocket(client);
#else
    close(client);
#endif /* win32 */
}

void handleClient(SOCK client, string clientId) {
    char buffer[1024] = { 0 };
    int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);

    if (bytes <= 0 || string(buffer) != PASSWORD) {
        string failMsg = "Authentication failed. Closing connection.";
        send(client, failMsg.c_str(), failMsg.length(), 0);
#ifdef _WIN32
    closesocket(client);
#else
    close(client);
#endif /* win32 */
        return;
    }
    // Match client's expected response ("AUTH_OK")
    string okMsg = "AUTH_OK";
    send(client, okMsg.c_str(), okMsg.length(), 0);

    {
        lock_guard<mutex> lock(clients_mutex);
        clients.push_back(client);
    }

    cout << "Client " << clientId << " connected and authenticated.\n";
    sendMessage("*** " + clientId + " has joined the chat. ***\n", client);

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_recd = recv(client, buffer, sizeof(buffer) - 1, 0);

        if (bytes_recd <= 0) {
            cout << "Client " << clientId << " disconnected." << endl;

            sendMessage("*** " + clientId + " has left the chat. ***\n", client);
            removeClient(client);
            break;
        }

        sendMessage(clientId + " > " + string(buffer) + "\n", client);
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif /* win32 */

    SOCK server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, 5);

    std::cout << "Server listening on port " << PORT << "...\n";
    int clientsCount = 0;

    while (true) {
        sockaddr_in client_addr;
        int client_size = sizeof(client_addr);

#ifdef _WIN32
        SOCK client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_size);
#else
        SOCK client_socket = accept(server_socket, (sockaddr*)&client_addr, (socklen_t*)&client_size);
#endif /* win32 */

        if (client_socket == INV_SOCK) {
            continue;
        }

        clientsCount++;
        string clientId = "Client" + to_string(clientsCount);
        thread client_thread(handleClient, client_socket, clientId);
        client_thread.detach();
    }

#ifdef _WIN32
    closesocket(server_socket);
    WSACleanup();
#else
    close(server_socket);
#endif /* _WIN32 */
    return 0;
}
