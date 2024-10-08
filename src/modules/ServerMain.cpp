#include "ServerMain.h"
#include <iostream>
#include <csignal>
#include "Config.h"
#include "CertificateManager.h"
#include "Server.h"

using namespace std;

/**
 * @brief Constructor for ServerMain class.
 * @details Initializes the ServerMain with a SocketManager instance and sets up the necessary resources.
 * Throws an exception if construction fails.
 */
ServerMain::ServerMain(){
    try {
        // Create a new SocketManager instance with specified configurations
        m_socket_manager = new SocketManager(Config::SERVER_IP, Config::SERVER_PORT,
                                             Config::MAX_REQUESTS);
    } catch (const std::exception& e) {
        cerr << "Exception caught during SocketManager construction: " << e.what() << endl;
        throw EXIT_FAILURE;
    }
}

/**
 * @brief Destructor for ServerMain class.
 * @details Cleans up resources, including deleting the SocketManager and CertificateManager instances,
 * and joining all threads in the thread pool.
 */
ServerMain::~ServerMain() {
    // Delete the SocketManager instance
    //delete m_socket_manager;

    // Delete the CertificateManager instance
    CertificateManager::deleteInstance();

    // Join all threads in the thread pool
    for (auto& thread : m_thread_pool)
        thread.join();
}

/**
 * @brief Getter for the SocketManager instance.
 * @return A reference to the SocketManager instance.
 */
SocketManager *ServerMain::getMSocketManager() const {
    return m_socket_manager;
}

/**
 * @brief Signal handler for the server.
 * @param signal The signal received by the server.
 * @details Handles signals like SIGINT and SIGPIPE. Exits the program on SIGINT and throws an exception on SIGPIPE.
 */
void ServerMain::serverSignalHandler(int signal) {
    if (signal == SIGINT) {
        cout << "Server closed!" << endl;
        exit(EXIT_SUCCESS);
    } else if (signal == SIGPIPE) {
        cout << "Server: SIGPIPE signal caught!" << endl;
        throw -3;
    }
}

/**
 * @brief Pushes a new thread into the thread pool.
 * @param socket_descriptor The socket descriptor for the new thread.
 * @details Creates a new SocketManager instance for the client and pushes a new thread into the thread pool
 * to handle the client connection.
 */
void ServerMain::emplaceThread(int socket_descriptor) {

    auto* socket = new SocketManager(socket_descriptor);
    // Emplace a new thread into the thread pool
    m_thread_pool.emplace_back([](SocketManager* thread_socket) {
        Server(thread_socket).run();
    }, socket);
}

/**
 * @brief Main function for the server.
 * @details Installs signal handlers, creates a SocketManager instance, and enters the main server loop,
 * accepting client connections and pushing threads to handle them.
 */
int main() {
    // Install the signal handler
    std::signal(SIGINT, ServerMain::serverSignalHandler);
    std::signal(SIGPIPE, ServerMain::serverSignalHandler);

    try {
        // Create a ServerMain instance
        ServerMain server_main;

        // Enter the main server loop
        while (true) {
            // Accept a client connection
            int socket_descriptor = server_main.getMSocketManager()->accept();
            if (socket_descriptor == -1) {
                cout << "ServerMain - Error during connection with the client!" << endl;
                continue;
            }
            // Push a new thread to handle the client connection
            server_main.emplaceThread(socket_descriptor);
        }
    } catch (int exit_code) {
        // Handle the exception from the constructor
        cerr << "Exception in main: " << exit_code << endl;
        return exit_code;
    }
}
