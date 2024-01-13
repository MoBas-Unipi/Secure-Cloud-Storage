#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "SocketManager.h"

/**
 * Initializes and configures a socket.
 *
 * @param ip_address IP address to bind the socket to.
 * @param port Port number to bind the socket to.
 * @param server_address Structure to hold server address information.
 *
 * @return The socket descriptor on success, or -1 on error.
 */
int SocketManager::initSocket(const string &ip_address, int port, sockaddr_in &server_address, bool isServer) {
    //socket creation
    if (isServer) {
        m_listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        m_socket = socket(AF_INET, SOCK_STREAM, 0);
    }

    //socket initialization (address creation)
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    inet_pton(AF_INET, ip_address.c_str(), &server_address.sin_addr);

    if (isServer) {
        return m_listening_socket;
    } else {
        return m_socket;
    }
}

/**
 * Server socket constructor
 * @param server_ip ip of the server
 * @param server_port port of the server
 * @param max_requests max number of clients simultaneously waiting
 */
SocketManager::SocketManager(const string &server_ip, int server_port, int max_requests) {
    sockaddr_in server_address{};

    //create and check the socket
    if (initSocket(server_ip, server_port, server_address, true) == -1) {
        cerr << "SocketManager - Error during socket creation!" << endl;
    }

    // Set SO_REUSEADDR option to avoid binding errors
    int opt = 1;
    setsockopt(m_listening_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    //binding the socket to an address
    if (bind(m_listening_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        cerr << "SocketManager - Error while binding socket!" << endl;
    }

    //opening the socket for requests listening
    if (listen(m_listening_socket, max_requests) == -1) {
        cerr << "SocketManager - Error while opening the socket!" << endl;
    }
}

/**
 * Client socket constructor
 * @param server_ip ip of the server
 * @param server_port ip of the server
 */
SocketManager::SocketManager(const string &server_ip, int server_port) {
    sockaddr_in server_address{};

    //create and check the socket
    if (initSocket(server_ip, server_port, server_address, false) == -1) {
        cerr << "SocketManager - Error during socket creation!" << endl;
    }

    // connection request to the server socket
    if (connect(m_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        cerr << "SocketManager - Error during the connection request" << endl;
    }
}

SocketManager::SocketManager(int socket_descriptor) : m_socket(socket_descriptor) {
}

SocketManager::~SocketManager() {
    close(m_socket);
    close(m_listening_socket);
}

int SocketManager::send(uint8_t *message_buffer, size_t message_buffer_size) {
    int result = ::send(m_socket, message_buffer, message_buffer_size, 0);
    if (result == -1) {
        cerr << "SocketManager - Error while sending the message" << endl;
        return -1;
    }
    return 0;
}

int SocketManager::receive(uint8_t *message_buffer, size_t message_buffer_size) {
    int result = recv(m_socket, message_buffer, message_buffer_size, MSG_WAITALL);
    if (result == 0) {
        cerr << "SocketManager - Error: connection closed!" << endl;
        return -1;
    } else if (result == -1) {
        cerr << "SocketManager - Error while receiving the message!" << endl;
        return -1;
    } else if (result != message_buffer_size) {
        cerr << "SocketManager - Error: incorrect message size!" << endl;
        return -1;
    } else {
        return 0;
    }
}

int SocketManager::accept() const {
    sockaddr_in client_address{};
    int client_address_size = sizeof(client_address);
    int socket_descriptor = ::accept(m_listening_socket, (struct sockaddr *) &client_address,
                                     (unsigned int *) &client_address_size);
    if (socket_descriptor == -1) {
        cerr << "SocketManager - Error during the connection request handling!" << endl;
    }
    return socket_descriptor;
}

