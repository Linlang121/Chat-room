#include "client.h"
#include <cstring>

static const char* PREFIXES[] = { "clients/", "public/", "private/" };
static const size_t NUM_PREFIXES = 3;

static size_t findNextPrefix(const std::string& s, size_t start) {
    size_t pos = std::string::npos;
    for (size_t i = 0; i < NUM_PREFIXES; i++) {
        size_t p = s.find(PREFIXES[i], start);
        if (p != std::string::npos && (pos == std::string::npos || p < pos))
            pos = p;
    }
    return pos;
}

Client::Client()
    : socket_(INVALID_SOCKET)
    , host_(HOST)
    , port_(PORT)
    , running_(false)
    , is_initializing_(false)
    , wsa_started_(false) {
}

Client::~Client() {
    disconnect();
}

void Client::extractMessages(std::string& buffer) {
    while (!buffer.empty()) {
        size_t start = findNextPrefix(buffer, 0);
        if (start == std::string::npos)
            break;
        size_t next = findNextPrefix(buffer, start + 1);
        std::string msg;
        if (next != std::string::npos) {
            msg = buffer.substr(start, next - start);
            buffer.erase(0, next);
        } else {
            msg = buffer.substr(start);
            buffer.clear();
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            message_queue_.push_back(msg);
        }
    }
}

void Client::recvThreadFunc() {
    std::string recv_buffer;
    char buf[RECV_BUF_SIZE];
    while (running_) {
        int n = recv(socket_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            recv_buffer += buf;
            extractMessages(recv_buffer);
        } else if (n == 0) {
            break;
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                Sleep(10);
                continue;
            }
            break;
        }
    }
    if (!recv_buffer.empty()) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        message_queue_.push_back(recv_buffer);
    }
    running_ = false;
}

void Client::initializeAsync(const std::string& username) {
    if (is_initializing_.load() || running_.load())
        return;
    is_initializing_.store(true);
    std::thread([this, username]() {
        do {
            WSADATA wsaData = {};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                wsa_started_ = false;
                break;
            }
            wsa_started_ = true;
            socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socket_ == INVALID_SOCKET) {
                WSACleanup();
                wsa_started_ = false;
                is_initializing_.store(false);
                return;
            }
            sockaddr_in server_address = {};
            server_address.sin_family = AF_INET;
            server_address.sin_port = htons(static_cast<u_short>(port_));
            if (inet_pton(AF_INET, host_.c_str(), &server_address.sin_addr) <= 0) {
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
                WSACleanup();
                wsa_started_ = false;
                is_initializing_.store(false);
                return;
            }
            if (connect(socket_, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == SOCKET_ERROR) {
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
                WSACleanup();
                wsa_started_ = false;
                is_initializing_.store(false);
                return;
            }
            if (send(socket_, username.c_str(), static_cast<int>(username.size()), 0) == SOCKET_ERROR) {
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
                WSACleanup();
                wsa_started_ = false;
                is_initializing_.store(false);
                return;
            }
            running_ = true;
            recv_thread_ = std::thread(&Client::recvThreadFunc, this);
        } while (0);
        is_initializing_.store(false);
    }).detach();
}

bool Client::isConnected() const {
    return socket_ != INVALID_SOCKET && running_.load();
}

bool Client::hasNewMessages() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !message_queue_.empty();
}

std::vector<std::string> Client::getNewMessages() {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    out.swap(message_queue_);
    return out;
}

bool Client::sendMessage(const std::string& msg) {
    if (socket_ == INVALID_SOCKET || !running_.load())
        return false;
    int sent = send(socket_, msg.c_str(), static_cast<int>(msg.size()), 0);
    return sent != SOCKET_ERROR && sent == static_cast<int>(msg.size());
}

void Client::reset() {
    running_ = false;
    if (recv_thread_.joinable())
        recv_thread_.join();
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (wsa_started_) {
        WSACleanup();
        wsa_started_ = false;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    message_queue_.clear();
}

void Client::disconnect() {
    if (socket_ != INVALID_SOCKET && running_.load()) {
        send(socket_, BYE, static_cast<int>(strlen(BYE)), 0);
    }
    running_ = false;
    if (recv_thread_.joinable())
        recv_thread_.join();
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (wsa_started_) {
        WSACleanup();
        wsa_started_ = false;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    message_queue_.clear();
}
