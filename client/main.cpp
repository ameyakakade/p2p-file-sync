#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define PASSWORD "secret123"

using namespace std;

vector<SOCKET> clients;
mutex clients_mutex;

void sendMessage(const string& msg, SOCKET sender) {
    lock_guard<mutex> lock(clients_mutex);
    for (SOCKET client : clients) {
        if (client != sender) {
            send(client, msg.c_str(), msg.length(), 0);
        }
    }
}

void removeClient(SOCKET client) {
    lock_guard<mutex> lock(clients_mutex);
    clients.erase(remove(clients.begin(), clients.end(), client), clients.end());
    closesocket(client);
}

void handleClient(SOCKET client, string clientId) {
    char buffer[1024] = { 0 };
    int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);

    if (bytes <= 0 || string(buffer) != PASSWORD) {
        string failMsg = "Authentication failed. Closing connection.";
        send(client, failMsg.c_str(), failMsg.length(), 0);
        closesocket(client);
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
    listen(server_socket, 5);

    std::cout << "Server listening on port " << PORT << "...\n";
    int clientsCount = 0;

    while (true) {
        sockaddr_in client_addr;
        int client_size = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_size);
        if (client_socket == INVALID_SOCKET) {
            continue;
        }

        clientsCount++;
        string clientId = "Client" + to_string(clientsCount);
        thread client_thread(handleClient, client_socket, clientId);
        client_thread.detach();
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}