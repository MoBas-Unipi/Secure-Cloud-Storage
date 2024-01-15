#ifndef SECURE_CLOUD_STORAGE_CLIENT_H
#define SECURE_CLOUD_STORAGE_CLIENT_H

#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <arpa/inet.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#include "SocketManager.h"
#include "Config.h"


class Client {

    string m_username;
    SocketManager* m_socket;
    EVP_PKEY* m_long_term_private_key;
    unsigned char m_session_key;
    uint32_t m_counter;

public:
    Client();
    ~Client();

    int run();
    void showMenu();
};


#endif //SECURE_CLOUD_STORAGE_CLIENT_H
