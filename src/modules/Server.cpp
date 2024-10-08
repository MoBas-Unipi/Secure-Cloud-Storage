#include <iostream>
#include <filesystem>
#include <openssl/pem.h>

#include "Generic.h"
#include "Server.h"
#include "CodesManager.h"
#include "List.h"
#include "Download.h"
#include "Upload.h"
#include "Rename.h"
#include "FileManager.h"
#include "SimpleMessage.h"
#include "Delete.h"
#include "Authentication.h"
#include "DiffieHellman.h"
#include "Hash.h"
#include "CertificateManager.h"
#include "AesGcm.h"
#include "DigitalSignatureManager.h"

using namespace std;

Server::Server(SocketManager *socket) {
    m_socket = socket;
}

Server::~Server() {
    OPENSSL_cleanse(m_session_key, Config::AES_KEY_LEN);
    OPENSSL_cleanse(m_socket, sizeof(m_socket));
}

void Server::incrementCounter() {
    // Check if re-authenticationRequest is needed
    if (m_counter == Config::MAX_COUNTER_VALUE) {
        if (authenticationRequest() != static_cast<int>(Return::AUTHENTICATION_SUCCESS)) {
            throw static_cast<int>(Return::AUTHENTICATION_FAILURE);
        }
    } else {
        m_counter++;
    }
}


/**
 * @brief Handle an authentication request from the client.
 * 1) Receive and deserialize an AuthenticationM1 message with the client's username and its ephemeral key (g^a mod p)
 * 2) Check if the client is registered (its public key is present in the storage) and send an AuthenticationM2 message
 * to him with the result
 * 3) If the client is not present: create and serialize a SimpleMessage with the result and send it to the client
 * 3) If the client is present:
 * 3.1) Generate the server ephemeral key (g^b mod p)
 * 3.2) Deserialize the client ephemeral key and derive the shared secret using the server and the client ephemeral keys
 * 3.3) Generate the session key from the shared secret
 * 3.4) Serialize the server ephemeral key and create a buffer for concatenating client and server ephemeral keys (<g^a,g^b> message)
 * 3.5) Generate digital signature for the message using server private key (<g^a,g^b>S)
 * 3.6) Encrypt all the message {<g^a,g^b>S}K_session
 * 3.7) Load the Server's certificate and serialize it
 * 3.8) Create AuthenticationM3 message and send it to the client
 * 4) Receive an AuthenticationM4 message from the client and deserialize it
 * 4.1) Decrypt the message and verify the client digital signature using the client public key
 * 5) Create an AuthenticationM5 SimpleMessage with the result, serialize it and send it to the client.
 *
 * @return An integer code indicating the result of the authentication process.
 */
int Server::authenticationRequest() {
    // Authentication M1 message
    cout << "Authentication request received" << endl;
    size_t authentication_m1_length = AuthenticationM1::getMessageSize();
    uint8_t* serialized_message = new uint8_t[authentication_m1_length];
    int result = m_socket->receive(serialized_message, authentication_m1_length);
    if (result != 0) {
        delete[] serialized_message;
        return static_cast<int>(Return::RECEIVE_FAILURE);
    }

    AuthenticationM1 authenticationM1 = AuthenticationM1::deserialize(serialized_message);
    OPENSSL_cleanse(serialized_message, authentication_m1_length);

    // Authentication M2 message
    string username_file = "../resources/public_keys/" + (string)authenticationM1.getMUsername() + "_key.pem";
    BIO *bio = BIO_new_file(username_file.c_str(), "r");
    EVP_PKEY* client_public_key = nullptr;
    SimpleMessage simple_message;
    size_t serialized_message_length;
    if (!bio) {
        BIO_free(bio);
        cerr << "Authentication - Error in creating the bio structure for the Client public key!" << endl;
        return static_cast<int>(Return::AUTHENTICATION_FAILURE);
    }
    m_username = (string)authenticationM1.getMUsername();
    client_public_key = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    if (!client_public_key) {
        simple_message.setMMessageCode(static_cast<int>(Result::NACK));
        cerr << "Authentication - Username " << m_username << " not found!" << endl;
    } else {
        simple_message.setMMessageCode(static_cast<int>(Result::ACK));
    }
    BIO_free(bio);

    // Serialize and send the acknowledgment or non-acknowledgment
    serialized_message = simple_message.serialize();
    serialized_message_length = SimpleMessage::getMessageSize();
    result = m_socket->send(serialized_message, serialized_message_length);
    OPENSSL_cleanse(serialized_message, serialized_message_length);
    if (result == -1) {
        EVP_PKEY_free(client_public_key);
        return static_cast<int>(Return::SEND_FAILURE);
    }
    if (simple_message.getMMessageCode() != static_cast<int>(Result::ACK)) {
        EVP_PKEY_free(client_public_key);
        return static_cast<int>(Error::USERNAME_NOT_FOUND);
    }

    // Authentication M3

    // Generate ephemeral key and derive shared secret
    DiffieHellman dh_instance;
    EVP_PKEY* server_ephemeral_key = dh_instance.generateEphemeralKey();

    EVP_PKEY* client_ephemeral_key = dh_instance.deserializeEphemeralKey(
            const_cast<uint8_t *>(authenticationM1.getMEphemeralKey()),
            authenticationM1.getMEphemeralKeyLen());
    uint8_t* shared_secret = nullptr;
    size_t shared_secret_length;
    result = dh_instance.deriveSharedSecret(server_ephemeral_key, client_ephemeral_key,
                                            shared_secret, shared_secret_length);
    EVP_PKEY_free(client_ephemeral_key);
    if (result != 0) {
        OPENSSL_cleanse(shared_secret, shared_secret_length);
        delete[] shared_secret;
        EVP_PKEY_free(server_ephemeral_key);
        EVP_PKEY_free(client_public_key);
        return static_cast<int>(Return::AUTHENTICATION_FAILURE);
    }

    // Generate session key from shared secret
    unsigned char* session_key = nullptr;
    unsigned int session_key_length;
    Hash::generateSHA256(shared_secret, shared_secret_length, session_key,
                         session_key_length);

    // Copy session key and clean up
    memcpy(m_session_key, session_key, Config::AES_KEY_LEN * sizeof(unsigned char));
    OPENSSL_cleanse(shared_secret, shared_secret_length);
    delete[] shared_secret;
    OPENSSL_cleanse(session_key, session_key_length);
    delete[] session_key;

    // Serialize server ephemeral key
    uint8_t* serialized_server_ephemeral_key = nullptr;
    int serialized_server_ephemeral_key_length = 0;
    result = dh_instance.serializeEphemeralKey(server_ephemeral_key, serialized_server_ephemeral_key,
                                               serialized_server_ephemeral_key_length);
    EVP_PKEY_free(server_ephemeral_key);
    if (result != 0) {
        OPENSSL_cleanse(serialized_server_ephemeral_key, serialized_server_ephemeral_key_length);
        delete[] serialized_server_ephemeral_key;
        return static_cast<int>(Return::AUTHENTICATION_FAILURE);
    }

    // Create a buffer for concatenating client and server ephemeral keys
    int ephemeral_key_buffer_length = authenticationM1.getMEphemeralKeyLen() + serialized_server_ephemeral_key_length;
    uint8_t* ephemeral_key_buffer = new uint8_t [ephemeral_key_buffer_length];
    memcpy(ephemeral_key_buffer, authenticationM1.getMEphemeralKey(),
           authenticationM1.getMEphemeralKeyLen());
    memcpy(ephemeral_key_buffer + authenticationM1.getMEphemeralKeyLen(), serialized_server_ephemeral_key,
           serialized_server_ephemeral_key_length);

    string private_key_file = "../resources/private_keys/Server_key.pem";
    bio = BIO_new_file(private_key_file.c_str(), "r");
    if (!bio) {
        BIO_free(bio);
        cerr << "Authentication - Error in creating the bio structure for the Server private key!" << endl;
        return static_cast<int>(Return::AUTHENTICATION_FAILURE);
    }
    EVP_PKEY* server_private_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if(!server_private_key) {
        EVP_PKEY_free(client_public_key);
        cerr << "Server private key not found!" << endl;
        return static_cast<int>(Return::AUTHENTICATION_FAILURE);
    }

    // Generate digital signature using server private key
    unsigned char* digital_signature = nullptr;
    unsigned int digital_signature_length;
    DigitalSignatureManager digitalSignatureManager;
    digitalSignatureManager.generateDS(ephemeral_key_buffer, ephemeral_key_buffer_length,
                                       digital_signature, digital_signature_length, server_private_key);
    EVP_PKEY_free(server_private_key);

    // Encrypt the digital signature for transmission in AuthenticationM3
    unsigned char *ciphertext = nullptr;
    m_counter = 0;
    unsigned char aad[sizeof(uint32_t)];
    memcpy(aad, &m_counter, Config::AAD_LEN);
    unsigned char tag[Config::AES_TAG_LEN];
    AesGcm aesGcm = AesGcm(m_session_key);
    int ciphertext_length = aesGcm.encrypt(digital_signature, ENCRYPTED_SIGNATURE_LEN,
                                           aad, Config::AAD_LEN,
                                           ciphertext, tag);
    delete[] digital_signature;

    // Check encryption failure
    if (ciphertext_length == -1) {
        delete[] serialized_message;
        delete[] serialized_server_ephemeral_key;
        delete[] ciphertext;
        delete[] ephemeral_key_buffer;
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }

    // Load server certificate and serialize it
    const char *certificate_file = "../resources/certificates/Server_cert.pem";
    CertificateManager* certificateManager = CertificateManager::getInstance();
    X509* certificate = certificateManager->loadCertificate(certificate_file);

    uint8_t* serialized_certificate = nullptr;
    int serialized_certificate_length = 0;
    certificateManager->serializeCertificate(certificate, serialized_certificate, serialized_certificate_length);
    X509_free(certificate);


    // Create AuthenticationM3 message and send it
    AuthenticationM3 authenticationM3(serialized_server_ephemeral_key,
                                      serialized_server_ephemeral_key_length, aesGcm.getIV(),
                                      aad, tag, ciphertext,
                                      serialized_certificate, serialized_certificate_length);
    serialized_message = authenticationM3.serialize();
    result = m_socket->send(serialized_message, authenticationM3.getMessageSize());
    OPENSSL_cleanse(serialized_message, serialized_message_length);
    delete[] serialized_certificate;
    delete[] serialized_server_ephemeral_key;
    delete[] ciphertext;
    if (result != 0) {
        EVP_PKEY_free(client_public_key);
        delete[] ephemeral_key_buffer;
        return static_cast<int>(Return::SEND_FAILURE);
    }
    incrementCounter();

    // AuthenticationM4
    serialized_message_length = AuthenticationM4::getMessageSize();
    serialized_message = new uint8_t[serialized_message_length];
    result = m_socket->receive(serialized_message, serialized_message_length);
    if (result != 0) {
        delete[] serialized_message;
        delete[] ephemeral_key_buffer;
        EVP_PKEY_free(client_public_key);
        return static_cast<int>(Return::RECEIVE_FAILURE);
    }

    // Deserialize AuthenticationM4
    AuthenticationM4 authenticationM4 = AuthenticationM4::deserialize(serialized_message);
    OPENSSL_cleanse(serialized_message, serialized_message_length);

    // Check counter value
    if(!authenticationM4.checkCounter(m_counter)) {
        delete[] serialized_message;
        delete[] ephemeral_key_buffer;
        return static_cast<int>(Return::WRONG_COUNTER);
    }

    incrementCounter();

    // Decrypt the digital signature in AuthenticationM4
    unsigned char* decrypted_signature = nullptr;
    unsigned int decrypted_signature_length = aesGcm.decrypt(
            const_cast<unsigned char *>(authenticationM4.getMEncryptedDigitalSignature()),
            ENCRYPTED_SIGNATURE_LEN * sizeof(uint8_t),
            (unsigned char *) authenticationM4.getMAad(),
            Config::AAD_LEN, const_cast<unsigned char *>(authenticationM4.getMIv()),
            (unsigned char *) authenticationM4.getMTag(), decrypted_signature);

    // Check decryption failure
    if (decrypted_signature_length == -1) {
        delete[] ephemeral_key_buffer;
        delete[] serialized_message;
        delete[] decrypted_signature;
        cerr << "AuthenticationM4 - Error during the decryption!" << endl;
        return static_cast<int>(Return::DECRYPTION_FAILURE);
    }

    // Verify client's digital signature
    bool isSignatureVerified = digitalSignatureManager.isDSverified(ephemeral_key_buffer,
                                                                    ephemeral_key_buffer_length,
                                                                    decrypted_signature,
                                                                    decrypted_signature_length,
                                                                    client_public_key);
    delete[] ephemeral_key_buffer;
    delete[] decrypted_signature;
    EVP_PKEY_free(client_public_key);

    // Check signature verification result
    if (!isSignatureVerified) {
        delete[] serialized_message;
        cerr << "Authentication - Client Signature not verified!" << endl;
    }

    // AuthenticationM5
    // Create a SimpleMessage with ACK/NACK code
    if (isSignatureVerified) {
        simple_message.setMMessageCode(static_cast<int>(Result::ACK));
    } else {
        simple_message.setMMessageCode(static_cast<int>(Result::NACK));
    }

    // Determine the size of the plaintext and ciphertext
    serialized_message_length = SimpleMessage::getMessageSize();
    // Serialize the SimpleMessage to obtain a byte buffer
    serialized_message = simple_message.serialize();
    // Create a Generic message with the current counter value
    Generic generic_msg1(m_counter);
    // Encrypt the serialized plaintext and init the GenericMessage fields
    if (generic_msg1.encrypt(m_session_key, serialized_message,
                             static_cast<int>(serialized_message_length)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }
    // Serialize Generic message
    serialized_message = generic_msg1.serialize();
    // Send the Generic message to the client
    if (m_socket->send(serialized_message,
                       Generic::getMessageSize(serialized_message_length)) == -1) {
        delete[] serialized_message;
        return static_cast<int>(Return::SEND_FAILURE);
    }

    delete[] serialized_message;
    //incrementCounter();
    m_counter = 0;

    // Output result based on signature verification
    if (isSignatureVerified) {
        cout << "Authentication request finished with code " << static_cast<int>(Return::AUTHENTICATION_SUCCESS) << endl;
        return static_cast<int>(Return::AUTHENTICATION_SUCCESS);
    } else {
        cout << "Authentication request finished with code " << static_cast<int>(Return::AUTHENTICATION_FAILURE) << endl;
        return static_cast<int>(Return::AUTHENTICATION_FAILURE);
    }
}

/**
 * @brief Handles a request from a client to list files in the user's folder and sends the file list.
 *
 * This function performs the following steps:
 * 1. Safely cleans the message buffer for the first message (ListM1).
 * 2. Sends a response message (ListM2) containing the size of the file list.
 *    a. If the user has no files, ends the function with success.
 *    b. If the list size is not 0, proceeds to the next step.
 * 3. Sends a response message (ListM3) containing the actual file list.
 *
 * @param plaintext The received message containing the client's list request.
 * @return An integer code indicating the result of the server's handling of the request.
 */
int Server::listRequest(uint8_t *plaintext) {
    // Safely clean plaintext buffer
    OPENSSL_cleanse(plaintext, SimpleMessage::getMessageSize());
    delete[] plaintext;

    incrementCounter();

    // Send message ListM2

    // Get the list of files in the user's folder
    string files = FileManager::getFilesList("../data/" + m_username);
    if (files == "error") {
        return static_cast<int>(Return::WRONG_PATH);
    }
    // If the path is correct send the message

    // Define the list size
    uint32_t list_size = files.length();
    // Include the null terminator char if the string is not empty
    if (list_size != 0) {
        list_size++;
    }
    // Create the ListM2 message
    size_t list_msg2_len = ListM2::getMessageSize();
    ListM2 list_msg2(list_size);
    // Serialize the ListM2 message to obtain a byte buffer
    uint8_t *serialized_message = list_msg2.serialize();
    // Create a Generic message with the current counter value
    Generic generic_msg2(m_counter);
    // Encrypt the serialized plaintext and init the GenericMessage fields
    if (generic_msg2.encrypt(m_session_key, serialized_message,
                             static_cast<int>(list_msg2_len)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }
    // Serialize Generic message
    serialized_message = generic_msg2.serialize();
    if (m_socket->send(serialized_message,
                       Generic::getMessageSize(list_msg2_len)) == -1) {
        delete[] serialized_message;
        return static_cast<int>(Return::SEND_FAILURE);
    }
    delete[] serialized_message;

    incrementCounter();

    // If list size is 0 no other messages will be sent
    if (list_size == 0) {
        cout << "Server - The user has no files in the folder." << endl;
        return static_cast<int>(Return::SUCCESS);
    }

    // Send message ListM3

    // Get the file list in a byte buffer
    auto *file_list = new uint8_t[list_size];
    memcpy(file_list, files.c_str(), list_size);
    // Create the ListM3 message
    size_t list_msg3_len = ListM3::getMessageSize(list_size);
    ListM3 list_msg3(list_size, file_list);
    // Serialize the ListM3 message to obtain a byte buffer
    serialized_message = list_msg3.serialize(list_size);
    // Create a Generic message with the current counter value
    Generic generic_msg3(m_counter);
    // Encrypt the serialized plaintext and init the GenericMessage fields
    if (generic_msg3.encrypt(m_session_key, serialized_message,
                             static_cast<int>(list_msg3_len)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }
    // Serialize Generic message
    serialized_message = generic_msg3.serialize();
    if (m_socket->send(serialized_message,
                       Generic::getMessageSize(list_msg3_len)) == -1) {
        delete[] serialized_message;
        return static_cast<int>(Return::SEND_FAILURE);
    }
    delete[] serialized_message;

    incrementCounter();

    // Return success code if the end of the function is reached
    return static_cast<int>(Return::SUCCESS);
}

/**
 * @brief Handles a download request from a client and sends the requested file in chunks.
 *
 * This function performs the following steps:
 * 1. Receives the client's request message (DownloadM1).
 * 2. Validates the request and obtains the requested file path.
 * 3. Sends a response message (DownloadM2) to the client:
 *    a. If the file is found, sends DOWNLOAD_ACK with the file size.
 *    b. If the file is not found, sends FILE_NOT_FOUND with size 0.
 * 4. If the file is not found, the function ends; otherwise, proceeds to send file chunks.
 * 5. Sends file chunks (DownloadM3+i) to the client in sequential order.
 *
 * @param plaintext The received message containing client's download request.
 * @return An integer code indicating the result of the server's handling of the request.
 */
int Server::downloadRequest(uint8_t *plaintext) {
    // Receive message DownloadM1

    // Deserialize received message
    DownloadM1 download_msg1 = DownloadM1::deserialize(plaintext);
    // Safely clean plaintext buffer
    OPENSSL_cleanse(plaintext, Config::MAX_PACKET_SIZE);
    delete[] plaintext;

    incrementCounter();

    // Send message DownloadM2

    // Obtain file path
    string file_path = "../data/" + m_username + "/" + (string) download_msg1.getFilename();
    DownloadM2 download_msg2;
    FileManager *file_to_send;
    // Check if the file is present and correct
    if (FileManager::isFilePresent(file_path) &&
        filesystem::is_regular_file(filesystem::path(file_path)) &&
        !filesystem::is_symlink(filesystem::path(file_path))) {
        // If the file is present create the message with DOWNLOAD_ACK and the file size
        file_to_send = new FileManager(file_path, FileManager::OpenMode::READ);
        download_msg2 = DownloadM2(static_cast<uint8_t>(Message::DOWNLOAD_ACK),
                                   file_to_send->getFileSize());
    } else {
        // If the file is not present create the message with FILE_NOT_FOUND and size 0
        download_msg2 = DownloadM2(static_cast<uint8_t>(Error::FILE_NOT_FOUND), 0);
    }
    size_t download_msg2_len = DownloadM2::getMessageSize();
    // Serialize the ListM2 message to obtain a byte buffer
    uint8_t *serialized_message = download_msg2.serialize();
    // Create a Generic message with the current counter value
    Generic generic_msg2(m_counter);
    // Encrypt the serialized plaintext and init the GenericMessage fields
    if (generic_msg2.encrypt(m_session_key, serialized_message,
                             static_cast<int>(download_msg2_len)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }
    // Serialize Generic message
    serialized_message = generic_msg2.serialize();
    if (m_socket->send(serialized_message,
                       Generic::getMessageSize(download_msg2_len)) == -1) {
        delete[] serialized_message;
        return static_cast<int>(Return::SEND_FAILURE);
    }
    delete[] serialized_message;

    incrementCounter();

    // If the file it is not found, no other messages are sent
    if (download_msg2.getFileSize() == 0) {
        return static_cast<int>(Return::FILE_NOT_FOUND);
    }

    // Send the message DownloadM3+i

    // Define the chunk size and buffer
    streamsize chunk_size = Config::CHUNK_SIZE;
    auto *current_chunk = new uint8_t[chunk_size];

    // Send each chunk of the file to the Client
    for (size_t i = 0; i < file_to_send->getChunksNum(); i++) {
        // If the chunk is the last, set the appropriate size
        if (i == file_to_send->getChunksNum() - 1) {
            chunk_size = file_to_send->getLastChunkSize();
        }
        // Send the message DownloadMi to the Client

        // Read the current chunk from the file
        if (file_to_send->readChunk(current_chunk, chunk_size) == -1) {
            return static_cast<int>(Return::READ_CHUNK_FAILURE);
        }
        // Determine the size of the message
        size_t download_msg3i_len = DownloadMi::getMessageSize(chunk_size);
        DownloadMi download_msg3i(current_chunk, chunk_size);
        // Serialize the DownloadMi message to obtain a byte buffer
        serialized_message = download_msg3i.serialize(chunk_size);
        // Create a Generic message with the current counter value
        Generic generic_msg3i(m_counter);
        // Encrypt the serialized plaintext and init the GenericMessage fields
        if (generic_msg3i.encrypt(m_session_key, serialized_message,
                                  static_cast<int>(download_msg3i_len)) == -1) {
            return static_cast<int>(Return::ENCRYPTION_FAILURE);
        }
        // Serialize Generic message
        serialized_message = generic_msg3i.serialize();
        if (m_socket->send(serialized_message,
                           Generic::getMessageSize(download_msg3i_len)) == -1) {
            delete[] serialized_message;
            return static_cast<int>(Return::SEND_FAILURE);
        }
        delete[] serialized_message;

        incrementCounter();
    }
    delete file_to_send;

    // Return success code if the end of the function is reached
    return static_cast<int>(Return::SUCCESS);
}


/**
 * Server side upload request operation
 * 1) Waits an upload message request from the client specifying file name and file size (UploadM1 message type)
 * 2) Send a response to the client indicating the success or failure of the upload request (SimpleMessage)
 * 3) Wait for the chunks inside M3+i messages and write into the file (UploadMi Message)
 * 4) Send the final response to the client after writing all file chunks in the file, indicating the overall success
 * or failure of the file upload (SimpleMessage)
 *
 * @param plaintext The message containing the file name and file size
 * @return An integer value representing the success or failure of the upload process.
 */
int Server::uploadRequest(uint8_t *plaintext) {

    // 1) Receive the upload request message M1 (UploadM1 message)
    UploadM1 upload_msg1 = UploadM1::deserializeUploadM1(plaintext);
    // Safely clean plaintext buffer
    OPENSSL_cleanse(plaintext, UploadM1::getSizeUploadM1());
    delete[] plaintext;

    // Increment counter against replay attack
    incrementCounter();


    // 2) Send the success message (if file does not exist) or fail message (if file exist) M2 (SimpleMessage)
    SimpleMessage upload_msg2;
    // Check if the file already exists, otherwise create the message to send
    string file_path = "../data/" + m_username + "/" + (string)upload_msg1.getFilename();
    if (FileManager::isFilePresent(file_path)) {
        cout << "Server - Error during upload request! File already exists" << endl;
        upload_msg2 = SimpleMessage(static_cast<uint8_t>(Result::NACK));
    }
    else {
        // Create success message to send to the Client
        upload_msg2 = SimpleMessage(static_cast<uint8_t>(Result::ACK));
    }

    // Serialize the message to send to the Client
    uint8_t* serialized_message = upload_msg2.serialize();
    // Determine the size of the message to send
    size_t upload_msg2_len = SimpleMessage::getMessageSize();

    // Create a Generic message with the current counter value
    Generic generic_msg2(m_counter);
    // Encrypt the serialized plaintext and init the Generic message fields
    if (generic_msg2.encrypt(m_session_key, serialized_message,static_cast<int>(upload_msg2_len)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }
    // Serialize and Send Generic message (SimpleMessage)
    serialized_message = generic_msg2.serialize();
    if (m_socket->send(serialized_message,Generic::getMessageSize(upload_msg2_len)) == -1) {
        return static_cast<int>(Return::SEND_FAILURE);
    }

    //Free the memory allocated for UploadM1 message
    delete[] serialized_message;

    // Increment counter against replay attack
    incrementCounter();

    if (upload_msg2.getMMessageCode() == static_cast<uint8_t>(Result::NACK)) {
        return static_cast<int>(Error::FILENAME_ALREADY_EXISTS);
    }


    //3) Receive the file chunks messages M3+i from the Client (UploadMi)
    // Prepare the file reception
    FileManager file_to_upload(file_path, FileManager::OpenMode::WRITE);
    size_t file_size = upload_msg1.getFileSize();
    file_to_upload.initFileInfo(file_size);

    // Compute the chunk size and upload state variable to check the received size
    size_t chunk_size = Config::CHUNK_SIZE;
    streamsize bytes_received = 0;

    // Set an interval for progress updates (e.g., every 10%)
    const int progressUpdateInterval = 1;
    int lastPrintedProgress = -1;


    // Receive all file chunks and write to the file
    for (size_t i = 0; i < file_to_upload.getChunksNum(); ++i) {
        //Receive the M3+i message
        // Get the chunk size
        if (i == file_to_upload.getChunksNum() - 1)
            chunk_size = file_to_upload.getLastChunkSize();


        // Determine the size of the message to receive
        size_t upload_msg3i_len = UploadMi::getSizeUploadMi(chunk_size);
        size_t generic_msg3i_len = Generic::getMessageSize(upload_msg3i_len);

        // Allocate memory for the buffer to receive the Generic message
        serialized_message = new uint8_t[generic_msg3i_len];
        if (m_socket->receive(serialized_message, generic_msg3i_len) == -1) {
            delete[] serialized_message;
            return static_cast<int>(Return::RECEIVE_FAILURE);
        }

        // Deserialize the received Generic message
        Generic generic_msg3i = Generic::deserialize(serialized_message, upload_msg3i_len);
        delete[] serialized_message;
        // Allocate memory for the plaintext buffer
        plaintext = new uint8_t[upload_msg3i_len];
        // Decrypt the Generic message to obtain the serialized message
        if (generic_msg3i.decrypt(m_session_key, plaintext) == -1) {
            return static_cast<int>(Return::DECRYPTION_FAILURE);
        }

        // Deserialize the upload message 3+i received (UploadMi)
        UploadMi upload_msg3i = UploadMi::deserializeUploadMi(plaintext, chunk_size);
        // Safely clean plaintext buffer
        OPENSSL_cleanse(plaintext, upload_msg3i_len);
        delete[] plaintext;

        // Check the counter value to prevent replay attacks
        if (m_counter != generic_msg3i.getCounter()) {
            return static_cast<int>(Return::WRONG_COUNTER);
        }

        // Increment counter against replay attack
        incrementCounter();


        // Write the received chunk in the file
        if (file_to_upload.writeChunk(upload_msg3i.getChunk(), chunk_size) == -1) {
            return static_cast<int>(Return::WRITE_CHUNK_FAILURE);
        }

        // Compute and show the progress to the user
        // Calculate upload progress percentage
        bytes_received += chunk_size;
        int newProgress = static_cast<int>((static_cast<double>(bytes_received) / static_cast<double>(file_size)) * 100);

        // Print progress only if it has changed or reached the specified interval
        if (newProgress != lastPrintedProgress && newProgress % progressUpdateInterval == 0) {
            cout << "\rServer - Uploading: " << newProgress << "% complete" << flush;
            lastPrintedProgress = newProgress;
        }
    }
    // Clear the progress message after completion
    cout << "\rServer - Uploading: 100% complete" << endl;


    // 4) Send the final packet M3+i+1 message (success file upload. Simple Message)
    SimpleMessage upload_msg3i1 = SimpleMessage(static_cast<uint8_t>(Result::ACK));

    // Serialize the message to send to the Client
    serialized_message = upload_msg3i1.serialize();
    // Determine the size of the message to send
    size_t upload_msg3i1_len = SimpleMessage::getMessageSize();

    // Create a Generic message with the current counter value
    Generic generic_msg3i1(m_counter);
    // Encrypt the serialized plaintext and init the Generic message fields
    if (generic_msg3i1.encrypt(m_session_key, serialized_message,static_cast<int>(upload_msg3i1_len)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }
    // Serialize and Send Generic message (SimpleMessage)
    serialized_message = generic_msg3i1.serialize();
    if (m_socket->send(serialized_message,Generic::getMessageSize(upload_msg3i1_len)) == -1) {
        return static_cast<int>(Return::SEND_FAILURE);
    }

    //Free the memory allocated for UploadM1 message
    delete[] serialized_message;

    // Increment counter against replay attack
    incrementCounter();


    // Successful upload
    return static_cast<int>(Return::SUCCESS);
}

/**
 * @brief Handle a file rename request on the server side.
 *
 * 1) Receive a file rename request from the client with the old filename and the new filename
 * 2) Rename the file
 * 3) Send the RenameM2 message with the result of the rename operation
 *
 * @param plaintext Pointer to the serialized data containing the RenameM1 message.
 *
 * @return An integer code indicating the result of the file rename operation.
 */
int Server::renameRequest(uint8_t *plaintext) {

    //RenameM1

    Rename renameM1 = Rename::deserializeRenameMessage(plaintext);
    // Safely clean plaintext buffer
    OPENSSL_cleanse(plaintext, SimpleMessage::getMessageSize());
    delete[] plaintext;

    incrementCounter();

    //RenameM2

    // Obtain file path
    string old_file_name_path = "../data/" + m_username + "/" + (string) renameM1.getMOldFilename();
    string new_file_name_path = "../data/" + m_username + "/" + (string) renameM1.getMNewFilename();
    SimpleMessage simple_message;
    // Check if the file is present and correct
    if (!FileManager::isFilePresent(old_file_name_path)){
        // If the file is not present create the message with FILE_NOT_FOUND
        simple_message = SimpleMessage(static_cast<uint8_t>(Return::FILE_NOT_FOUND));
    } else if (FileManager::isFilePresent(new_file_name_path)) {
        // If the file is already present create the message with FILE_ALREADY_EXISTS
        simple_message = SimpleMessage(static_cast<uint8_t>(Return::FILE_ALREADY_EXISTS));
    } else {
        if(rename((old_file_name_path).c_str(), (new_file_name_path).c_str()) != 0) {
            simple_message = SimpleMessage(static_cast<int>(Result::NACK));
        } else {
            simple_message = SimpleMessage(static_cast<int>(Result::ACK));
        }
    }

    size_t simple_message_length = SimpleMessage::getMessageSize();
    uint8_t* serialized_message = simple_message.serialize();

    Generic generic_msg2(m_counter);

    if (generic_msg2.encrypt(m_session_key, serialized_message,
                             static_cast<int>(simple_message_length)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }

    serialized_message = generic_msg2.serialize();
    if (m_socket->send(serialized_message,
                       Generic::getMessageSize(simple_message_length)) == -1) {
        delete[] serialized_message;
        return static_cast<int>(Return::SEND_FAILURE);
    }
    delete[] serialized_message;

    incrementCounter();

    // Return success code if the end of the function is reached
    return static_cast<int>(Return::SUCCESS);
}


/**
 * Server side delete request operation
 * 1) Waits delete message request from the client (Delete message type)
 * 2) Send a response to the client asking the confirmation (SimpleMessage "DELETE_ASK")
 * 3) Waits the delete confirmation message from the client (SimpleMessage "DELETE_CONFIRM") and then delete the file
 * 4) Send the final response to the client, indicating the overall success or failure
 * of the file delete (SimpleMessage)
 *
 * @param plaintext The message containing the file name of the file to delete
 * @return An integer value representing the success or failure of the upload process.
 */
int Server::deleteRequest(uint8_t *plaintext) {
    // 1) Receive the delete request message M1 (Delete message)
    Delete delete_msg1 = Delete::deserialize(plaintext);
    // Safely clean plaintext buffer
    OPENSSL_cleanse(plaintext, Delete::getMessageSize());
    delete[] plaintext;

    // Increment counter against replay attack
    incrementCounter();


    // 2) Send the delete confirmation message M2 (SimpleMessage "DELETE_ASK")
    SimpleMessage delete_msg2(static_cast<uint8_t>(Message::DELETE_ASK));


    // Serialize the message to send to the Client
    uint8_t* serialized_message = delete_msg2.serialize();
    // Determine the size of the message to send
    size_t delete_msg2_len = SimpleMessage::getMessageSize();

    // Create a Generic message with the current counter value
    Generic generic_msg2(m_counter);
    // Encrypt the serialized plaintext and init the Generic message fields
    if (generic_msg2.encrypt(m_session_key, serialized_message,static_cast<int>(delete_msg2_len)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }
    // Serialize and Send Generic message (SimpleMessage)
    serialized_message = generic_msg2.serialize();
    if (m_socket->send(serialized_message,Generic::getMessageSize(delete_msg2_len)) == -1) {
        return static_cast<int>(Return::SEND_FAILURE);
    }

    //Free the memory allocated for UploadM1 message
    delete[] serialized_message;

    // Increment counter against replay attack
    incrementCounter();


    // 3) Receive the Delete M3 message (Delete Ask confirmation message. Simple Message "DELETE_CONFIRM")
    // Determine the size of the message to receive
    size_t delete_msg3_len = SimpleMessage::getMessageSize();

    // Allocate memory for the buffer to receive the Generic message
    serialized_message = new uint8_t[Generic::getMessageSize(delete_msg3_len)];
    if (m_socket->receive(serialized_message, Generic::getMessageSize(delete_msg3_len)) == -1) {
        delete[] serialized_message;
        return static_cast<int>(Return::RECEIVE_FAILURE);
    }

    // Deserialize the received Generic message
    Generic generic_msg3 = Generic::deserialize(serialized_message, delete_msg3_len);
    delete[] serialized_message;
    // Allocate memory for the plaintext buffer
    plaintext = new uint8_t[delete_msg3_len];
    // Decrypt the Generic message to obtain the serialized message
    if (generic_msg3.decrypt(m_session_key, plaintext) == -1) {
        return static_cast<int>(Return::DECRYPTION_FAILURE);
    }

    // Deserialize the delete message 2 received (Simple Message)
    SimpleMessage delete_msg3 = SimpleMessage::deserialize(plaintext);
    // Safely clean plaintext buffer
    OPENSSL_cleanse(plaintext, delete_msg3_len);
    delete[] plaintext;

    // Check the counter value to prevent replay attacks
    if (m_counter != generic_msg3.getCounter()) {
        return static_cast<int>(Return::WRONG_COUNTER);
    }

    // Increment counter against replay attack
    incrementCounter();

    // Check the received message code
    if (delete_msg3.getMMessageCode() != static_cast<uint8_t>(Message::DELETE_CONFIRM)) {
        return static_cast<int>(Return::WRONG_MSG_CODE);
    }

    // Create the variables for file name and file path
    string file_name = (char*)delete_msg1.getFileName();
    string file_path = "../data/" + m_username + "/";

    // Check if the file with file_name exists
    if (!FileManager::isFilePresent(file_path+file_name)) {
        return static_cast<int>(Error::FILENAME_NOT_FOUND);
    }

    // Delete file and check the result
    if (remove((file_path + file_name).c_str()) != 0) {
        return static_cast<int>(Error::DELETE_FILE_ERROR);
    }


    // 4) Send the final Delete message M4 (success file deletion. Simple Message)
    SimpleMessage delete_msg4 = SimpleMessage(static_cast<uint8_t>(Result::ACK));

    // Serialize the message to send to the Client
    serialized_message = delete_msg4.serialize();
    // Determine the size of the message to send
    size_t delete_msg4_len = SimpleMessage::getMessageSize();

    // Create a Generic message with the current counter value
    Generic generic_msg4(m_counter);
    // Encrypt the serialized plaintext and init the Generic message fields
    if (generic_msg4.encrypt(m_session_key, serialized_message,static_cast<int>(delete_msg4_len)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }
    // Serialize and Send Generic message (SimpleMessage)
    serialized_message = generic_msg4.serialize();
    if (m_socket->send(serialized_message,Generic::getMessageSize(delete_msg4_len)) == -1) {
        return static_cast<int>(Return::SEND_FAILURE);
    }

    //Free the memory allocated for UploadM1 message
    delete[] serialized_message;

    // Increment counter against replay attack
    incrementCounter();


    // Successful delete
    return static_cast<int>(Return::SUCCESS);
}


/**
 * Server side logout request operation
 * 1) Waits a logout message request from the client (SimpleMessage)
 * 2) send a response to the client indicating the success or failure of the logout request (SimpleMessage)
 * @param plaintext The message containing the logout request
 * @return An integer value representing the success or failure of the upload process.
 */
int Server::logoutRequest(uint8_t *plaintext) {
    // Safely clean plaintext buffer
    OPENSSL_cleanse(plaintext, SimpleMessage::getMessageSize());
    delete[] plaintext;

    // Increment counter against replay attack
    incrementCounter();

    // Determine the size of the message to send
    size_t logout_msg2_len = SimpleMessage::getMessageSize();
    // 2) Send the success message (if file does not exist) or fail message (if file exists) M2 (SimpleMessage)
    SimpleMessage logout_msg2(static_cast<uint8_t>(Result::ACK));;
    // Check if the file already exists, otherwise create the message to send

    // Serialize the message to send to the Client
    uint8_t* serialized_message = logout_msg2.serialize();


    // Create a Generic message with the current counter value
    Generic generic_msg2(m_counter);
    // Encrypt the serialized plaintext and init the Generic message fields
    if (generic_msg2.encrypt(m_session_key, serialized_message,static_cast<int>(logout_msg2_len)) == -1) {
        return static_cast<int>(Return::ENCRYPTION_FAILURE);
    }

    //Delete The Session Key (Logout operation)
    OPENSSL_cleanse(m_session_key, sizeof(m_session_key));

    // Serialize and Send Generic message (SimpleMessage)
    serialized_message = generic_msg2.serialize();
    if (m_socket->send(serialized_message,Generic::getMessageSize(logout_msg2_len)) == -1) {
        delete[] serialized_message;
        return static_cast<int>(Return::SEND_FAILURE);
    }

    //Free the memory allocated for UploadM1 message
    delete[] serialized_message;

    // Successful logout
    return static_cast<int>(Return::SUCCESS);
}

void Server::run() {
    try {
        // Perform login
        int result = authenticationRequest();
        if (result != static_cast<int>(Return::AUTHENTICATION_SUCCESS)) {
            cout << "Server - Error! Login failed with error code: " << result << endl;
            return;
        }
        // Determine the expected size of the message buffer
        size_t message_size = Generic::getMessageSize(Config::MAX_PACKET_SIZE);
      
        while (true) {
            // Allocate memory for the buffer to receive the first message
            auto *serialized_message = new uint8_t[message_size];
            result = m_socket->receive(serialized_message, message_size);
            if (result == -1) {
                delete[] serialized_message;
                cout << "Server - Error! Receive failed" << endl;
                return;
            }
            if (result == -2) {
                delete[] serialized_message;
                cout << "Server - Connection Closed with user " << m_username << endl;
                return;
            }
            // Deserialize the received message
            Generic generic_message = Generic::deserialize(serialized_message,
                                                           Config::MAX_PACKET_SIZE);
            delete[] serialized_message;
            // Allocate memory for the plaintext
            auto *plaintext = new uint8_t[Config::MAX_PACKET_SIZE];

            // Decrypt the received ciphertext
            if (generic_message.decrypt(m_session_key, plaintext) == -1) {
                return;
            }
            // Check the counter value to prevent replay attacks
            if (m_counter != generic_message.getCounter()) {
                throw static_cast<int>(Return::WRONG_COUNTER);
            }
            // Taking the command code as the first byte of plaintext
            uint8_t command = plaintext[0];

            switch (command) {
                case static_cast<uint8_t>(Message::LIST_REQUEST):
                    cout << "Server - List request received" << endl;
                    result = listRequest(plaintext);
                    cout << "Server - List request finished with code " << result << endl;
                    break;

                case static_cast<uint8_t>(Message::DOWNLOAD_REQUEST):
                    cout << "Server - Download request received" << endl;
                    result = downloadRequest(plaintext);
                    cout << "Server - Download request finished with code " << result << endl;
                    break;

                case static_cast<uint8_t>(Message::UPLOAD_REQUEST):
                    cout << "Server - Upload request received" << endl;
                    result = uploadRequest(plaintext);
                    cout << "Server - Upload request finished with code " << result << endl;
                    break;

                case static_cast<uint8_t>(Message::RENAME_REQUEST):
                    cout << "Server - Rename request received" << endl;
                    result = renameRequest(plaintext);
                    cout << "Server - Rename request finished with code " << result << endl;
                    break;

                case static_cast<uint8_t>(Message::DELETE_REQUEST):
                    cout << "Server - Delete request received" << endl;
                    result = deleteRequest(plaintext);
                    cout << "Server - Delete request finished with code " << result << endl;
                    break;

                case static_cast<uint8_t>(Message::LOGOUT_REQUEST):
                    cout << "Server - Logout request received" << endl;
                    result = logoutRequest(plaintext);
                    cout << "Server - Logout request finished with code " << result << endl;
                    break;

                default:
                    cerr << "Server - Invalid command received." << endl;
                    break;
            }
        }
    } catch (int error_code) {
        cout << "Server - Operation failed with error code: " << error_code << endl;
    } catch (const exception &e) {
        cerr << "Server - Exception in run: " << e.what() << endl;
    }
}