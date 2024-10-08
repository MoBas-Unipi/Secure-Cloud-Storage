#ifndef SECURE_CLOUD_STORAGE_GENERIC_H
#define SECURE_CLOUD_STORAGE_GENERIC_H

#include <iostream>
#include <openssl/evp.h>
#include "Config.h"

class Generic {

private:
    unsigned char m_iv[Config::IV_LEN]{};
    unsigned char m_aad[Config::AAD_LEN]{};
    unsigned char m_tag[Config::AES_TAG_LEN]{};
    unsigned char *m_ciphertext{};
    int m_ciphertext_len{};

public:
    Generic();

    explicit Generic(uint32_t counter);

    ~Generic();

    int encrypt(unsigned char *session_key, unsigned char *plaintext, int plaintext_len);

    int decrypt(unsigned char *session_key, unsigned char *&plaintext);

    uint8_t *serialize();

    static Generic deserialize(uint8_t *message_buffer, size_t ciphertext_len);

    static size_t getMessageSize(size_t plaintext_len);

    void print(size_t plaintext_len) const;

    uint32_t getCounter() const;
};

#endif //SECURE_CLOUD_STORAGE_GENERIC_H