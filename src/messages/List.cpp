#include <cstring>
#include <iostream>
#include "List.h"
#include "CodesManager.h"
#include "Config.h"
#include <openssl/rand.h>

using namespace std;

// ListM2 Message

/**
 * Default constructor for ListM2
 */
ListM2::ListM2() = default;

/**
 * Constructor of ListM3 to be used in case of serialization
 */
ListM2::ListM2(uint32_t list_size) {
    m_message_code = static_cast<uint8_t>(Message::LIST_ACK);
    m_list_size = list_size;
}

/**
 * Serialize ListM2 message into a byte buffer
 * @return A dynamically allocated byte buffer containing the serialized message
 */
uint8_t *ListM2::serialize() {
    // Serialize ListM2 message into a byte buffer
    auto *buffer = new(nothrow) uint8_t[Config::MAX_PACKET_SIZE];
    if (!buffer) {
        cerr << "ListM2 - Error during serialization: Failed to allocate memory" << endl;
        return nullptr;
    }

    size_t position = 0;
    memcpy(buffer, &m_message_code, sizeof(m_message_code));
    position += sizeof(m_message_code);

    memcpy(buffer + position, &m_list_size, sizeof(m_list_size));

    return buffer;
}

/**
 * Deserialize a byte buffer into a ListM2 message
 * @param buffer The byte buffer to deserialize
 * @return A ListM2 object with the deserialized data
 */
ListM2 ListM2::deserialize(uint8_t *buffer) {
    // Deserialize ListM2 message
    ListM2 listM2Message;

    size_t position = 0;
    memcpy(&listM2Message.m_message_code, buffer, sizeof(m_message_code));
    position += sizeof(m_message_code);

    memcpy(&listM2Message.m_list_size, buffer + position, sizeof(m_list_size));

    return listM2Message;
}

/**
 * Get the size of the ListM2 message in bytes
 * @return The size of the ListM2 message
 */
size_t ListM2::getMessageSize() {
    return sizeof(m_message_code) +
           sizeof(m_list_size);
}

uint8_t ListM2::getMessageCode() const {
    return m_message_code;
}

uint32_t ListM2::getListSize() const {
    return m_list_size;
}

// ListM3 Message

/**
 * Default constructor for ListM3
 */
ListM3::ListM3() = default;

/**
 * Constructor of ListM3 to be used in case of serialization
 */
ListM3::ListM3(uint32_t list_size, uint8_t *file_list) {
    m_message_code = static_cast<uint8_t>(Message::LIST_RESPONSE);
    if (list_size > 0) {
        m_file_list = new uint8_t[list_size];
        memcpy(m_file_list, file_list, list_size);
    }
}

/**
 * Destructor for the ListM3 class
 */
ListM3::~ListM3() {
    delete[] m_file_list;
}

/**
 * Serialize ListM3 message into a byte buffer
 * @return A dynamically allocated byte buffer containing the serialized message
 */
uint8_t *ListM3::serialize(uint32_t list_size) {
    auto *buffer = new(nothrow) uint8_t[ListM3::getMessageSize(list_size)];
    if (!buffer) {
        cerr << "ListM3 - Error during serialization: Failed to allocate memory" << endl;
        return nullptr;
    }
    size_t position = 0;
    memcpy(buffer, &m_message_code, sizeof(m_message_code));
    position += sizeof(m_message_code);

    if (list_size > 0) {
        memcpy(buffer + position, m_file_list, list_size);
    }
    return buffer;
}

/**
 * Deserialize a byte buffer into a ListM3 message
 * @param buffer The byte buffer to deserialize
 * @param buffer_len The length of the buffer
 * @return A ListM3 object with the deserialized data
 */
ListM3 ListM3::deserialize(uint8_t *buffer, uint32_t list_size) {
    ListM3 listM3Message;

    size_t position = 0;
    memcpy(&listM3Message.m_message_code, buffer, sizeof(m_message_code));
    position += sizeof(m_message_code);

    if (list_size > 0) {
        listM3Message.m_file_list = new uint8_t[list_size];
        memcpy(listM3Message.m_file_list, buffer + position, list_size);
    }
    return listM3Message;
}

/**
 * Get the size of the ListM3 message in bytes
 * @return The size of the ListM3 message
 */
size_t ListM3::getMessageSize(uint32_t list_size) {
    // Sum of the message code size
    // plus the size of the list of files in bytes
    return sizeof(m_message_code) +
           list_size;
}

uint8_t ListM3::getMessageCode() const {
    return m_message_code;
}

uint8_t *ListM3::getFileList() const {
    return m_file_list;
}
