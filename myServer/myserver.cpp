#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>


#pragma comment(lib, "Ws2_32.lib")
int server() {
    // Step 1: Initialize WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Step 2: Create a socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Step 3: Bind the socket
    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(65432);  // Server port
    server_address.sin_addr.s_addr = INADDR_ANY; // Accept connections on any IP address

    if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Step 4: Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port 65432..." << std::endl;

    // Step 5: Accept a connection
    sockaddr_in client_address = {};
    int client_address_len = sizeof(client_address);
    SOCKET client_socket = accept(server_socket, (sockaddr*)&client_address, &client_address_len);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cout << "Accepted connection from " << client_ip << ":" << ntohs(client_address.sin_port) << std::endl;

    // Step 6: Communicate with the client
    char buffer[1024] = { 0 };
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        std::cout << "Received: " << buffer << std::endl;

        // Reverse the string
        std::string response(buffer);
        std::reverse(response.begin(), response.end());

        // Send the reversed string back
        send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
        std::cout << "Reversed string sent back to client." << std::endl;
    }

    // Step 7: Clean up
    closesocket(client_socket);
    closesocket(server_socket);
    WSACleanup();

    return 0;
}


int server_loop() {
    // Step 1: Initialize WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Step 2: Create a socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Step 3: Bind the socket
    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(65432);  // Server port
    server_address.sin_addr.s_addr = INADDR_ANY; // Accept connections on any IP address

    if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Step 4: Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port 65432..." << std::endl;

    // Step 5: Accept a connection
    sockaddr_in client_address = {};
    int client_address_len = sizeof(client_address);
    SOCKET client_socket = accept(server_socket, (sockaddr*)&client_address, &client_address_len);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cout << "Accepted connection from " << client_ip << ":" << ntohs(client_address.sin_port) << std::endl;

    bool stop = false;
    while (!stop) {
        // Step 6: Communicate with the client
        char buffer[1024] = { 0 };
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::cout << "Received: " << buffer << std::endl;

            // Reverse the string
            std::string response(buffer);
            if (response == "!bye") stop = true;

            std::reverse(response.begin(), response.end());

            // Send the reversed string back
            send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
            std::cout << "Reversed string sent back to client." << std::endl;
        }
    }
    std::cout << "Closing connection\n";

    // Step 7: Clean up
    closesocket(client_socket);
    closesocket(server_socket);
    WSACleanup();
    return 0;

}

void doClient(SOCKET client_socket, int connection) {
    bool stop = false;
    while (!stop) {
        // Step 6: Communicate with the client
        char buffer[1024] = { 0 };
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::cout << connection;
            std::cout << " : Received: " << buffer << std::endl;

            // Reverse the string
            std::string response(buffer);
            if (response == "!bye") stop = true;

            std::reverse(response.begin(), response.end());

            // Send the reversed string back
            send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
            std::cout << "Reversed string sent back to client " << connection << std::endl;
        }
    }
    std::cout << "Closing connection to client " << connection << "\n";
    closesocket(client_socket);
}


int server_loop_multi() {
    // Step 1: Initialize WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Step 2: Create a socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Step 3: Bind the socket
    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(65432);  // Server port
    server_address.sin_addr.s_addr = INADDR_ANY; // Accept connections on any IP address

    if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Step 4: Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port 65432..." << std::endl;

    // Step 5: Accept a connection
    int connection = 0;
    while (1) { // server keeps running
        sockaddr_in client_address = {};
        int client_address_len = sizeof(client_address);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_address, &client_address_len);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
            closesocket(server_socket);
            WSACleanup();
            return 1;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Accepted connection from " << client_ip << ":" << ntohs(client_address.sin_port);
        std::cout << " . Connection id = " << ++connection;

        std::thread* t = new std::thread(doClient, client_socket, connection);
        //    std::unique_ptr<std::thread> t = std::make_unique<std::thread>(doClient, client_socket, connection); 
         //   t->detach(); // probably best to free up resources by detaching
          // consider using a vector in order to keep all threads and free them eventually
            //   doClient(client_socket); 
             //  t.join();         
    }
    // Step 7: Clean up
    std::cout << "Server closed\n";
    closesocket(server_socket);
    WSACleanup();
    return 0;

}

int main() {
    // server();
    // server_loop(); 
    server_loop_multi();
    return 0;
}
