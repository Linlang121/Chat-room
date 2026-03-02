#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

class Client {
public:
    static constexpr const char* HOST = "127.0.0.1";
    static constexpr unsigned int PORT = 65432;
    static constexpr const char* BYE = "!bye";
    static constexpr int RECV_BUF_SIZE = 4096;

    Client();
    ~Client();

    void initializeAsync(const std::string& username);  /* 后台连服务器 + 收消息线程 */

    bool isConnected() const;
    bool isInitializing() const { return is_initializing_.load(); }

    bool hasNewMessages() const;
    std::vector<std::string> getNewMessages();

    bool sendMessage(const std::string& msg);

    void reset();       /* 重连前清掉 recv 线程/socket/队列 */
    void disconnect();  /* 发 !bye 并关 socket */

private:
    void recvThreadFunc();
    void extractMessages(std::string& buffer);  /* 按 clients/public/private 切消息入队 */

    SOCKET socket_;
    std::string host_;
    unsigned int port_;
    std::thread recv_thread_;
    std::vector<std::string> message_queue_;
    mutable std::mutex queue_mutex_;
    std::atomic<bool> running_;
    std::atomic<bool> is_initializing_;
    bool wsa_started_;
};
