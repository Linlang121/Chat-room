#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>

struct ClientInfo {
    SOCKET socket;
    std::string name;
};

class Server {
public:
    static constexpr const char* DISCONNECT = "!bye";

    Server();
    ~Server();

    bool start();
    bool stop();

    void acceptClients();
    void handleClient(SOCKET socket, std::string name);
    void parseMessage(SOCKET socket, const std::string& str);

    void broadcastMessage(SOCKET sender_socket, const std::string& message);
    void sendToClient(SOCKET client_socket, const std::string& message);

    size_t getClientCount();
    bool isRunning() const { return running_; }

private:
    SOCKET server_socket_;
    int port_;
    std::vector<ClientInfo> clients_;
    std::mutex clients_mutex_;
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    bool running_;

    static const int BUFFER_SIZE = 1024;
};
