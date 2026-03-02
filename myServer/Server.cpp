#pragma comment(lib, "ws2_32.lib")

#include "Server.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>

namespace {

std::string buildClientList(const std::vector<ClientInfo>& clients) {
    std::string list;
    for (const auto& c : clients)
        list += c.name + ",";
    return list;
}

} // namespace

Server::Server()
    : server_socket_(INVALID_SOCKET)
    , port_(65432)
    , running_(false) {
}

Server::~Server() {
    stop();
}

bool Server::start() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return false;
    }

    server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket_ == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    int opt = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
        std::cerr << "setsockopt SO_REUSEADDR failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket_);
        server_socket_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(static_cast<u_short>(port_));
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket_, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket_);
        server_socket_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    if (listen(server_socket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket_);
        server_socket_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&Server::acceptClients, this);
    return true;
}

void Server::acceptClients() {
    while (running_) {
        sockaddr_in client_address = {};
        int client_address_len = sizeof(client_address);
        SOCKET client_socket = accept(server_socket_, reinterpret_cast<sockaddr*>(&client_address), &client_address_len);
        if (client_socket == INVALID_SOCKET) {
            if (running_)
                std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
            continue;
        }

        char buffer[BUFFER_SIZE] = {};
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            closesocket(client_socket);
            continue;
        }
        buffer[bytes_received] = '\0';
        std::string username(buffer);

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back({ client_socket, username });
        }

        std::string clientList;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clientList = buildClientList(clients_);
        }
        sendToClient(client_socket, "clients/" + clientList);
        broadcastMessage(client_socket, "clients/" + clientList);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "New client: " << client_ip << ":" << ntohs(client_address.sin_port) << " username=\"" << username << "\"" << std::endl;

        client_threads_.emplace_back(&Server::handleClient, this, client_socket, username);
    }
}

void Server::handleClient(SOCKET socket, std::string name) {
    char buffer[BUFFER_SIZE] = {};
    while (running_) {
        int bytes_received = recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string message(buffer);
            if (message == DISCONNECT)
                break;
            parseMessage(socket, message);
            std::cout << "Received: " << message << std::endl;
        } else if (bytes_received == 0) {
            break;
        } else {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(),
                [socket](const ClientInfo& c) { return c.socket == socket; }),
            clients_.end());
    }

    std::string clientList;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clientList = buildClientList(clients_);
    }
    broadcastMessage(socket, "clients/" + clientList);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    closesocket(socket);
}

void Server::parseMessage(SOCKET socket, const std::string& str) {
    size_t pos = str.find('/');
    if (pos == std::string::npos)
        return;
    std::string type = str.substr(0, pos);
    std::string data = str.substr(pos + 1);

    if (type == "public") {
        broadcastMessage(socket, "public/" + data);
        return;
    }
    if (type == "private") {
        size_t colon = data.find(':');
        if (colon == std::string::npos)
            return;
        std::string targetName = data.substr(0, colon);
        std::string content = data.substr(colon + 1);

        std::string fromName;
        SOCKET target_socket = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (const auto& c : clients_) {
                if (c.socket == socket)
                    fromName = c.name;
                if (c.name == targetName)
                    target_socket = c.socket;
            }
        }
        if (target_socket != INVALID_SOCKET && !fromName.empty())
            sendToClient(target_socket, "private/" + fromName + ":" + content);
    }
}

void Server::broadcastMessage(SOCKET sender_socket, const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& c : clients_) {
        if (c.socket != sender_socket && c.socket != INVALID_SOCKET)
            send(c.socket, message.c_str(), static_cast<int>(message.size()), 0);
    }
}

void Server::sendToClient(SOCKET client_socket, const std::string& message) {
    if (client_socket != INVALID_SOCKET)
        send(client_socket, message.c_str(), static_cast<int>(message.size()), 0);
}

size_t Server::getClientCount() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

bool Server::stop() {
    if (!running_)
        return false;
    running_ = false;

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& c : clients_) {
            if (c.socket != INVALID_SOCKET) {
                send(c.socket, DISCONNECT, static_cast<int>(strlen(DISCONNECT)), 0);
                closesocket(c.socket);
                c.socket = INVALID_SOCKET;
            }
        }
        clients_.clear();
    }

    if (server_socket_ != INVALID_SOCKET) {
        closesocket(server_socket_);
        server_socket_ = INVALID_SOCKET;
    }

    if (accept_thread_.joinable())
        accept_thread_.join();

    for (auto& t : client_threads_)
        if (t.joinable())
            t.join();
    client_threads_.clear();

    WSACleanup();
    return true;
}
