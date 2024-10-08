#include <cmath>
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <string>

#include "FileManager.h"
#include "Config.h"

using namespace std;

/**
 * Default constructor for the FileManager class
 */
FileManager::FileManager() = default;

/**
 * Constructor for the FileManager class
 * @param file_path The path to the file
 * @param open_mode The mode in which the file should be opened (READ or WRITE)
 */
FileManager::FileManager(const string &file_path, OpenMode open_mode)
        : m_open_mode(open_mode), m_in_file(), m_out_file(),
          m_file_size(0), m_chunks_num(0), m_last_chunk_size(0) {
    openFile(file_path);
}

/**
 * Destructor for the FileManager class
 * Closes the file if it was opened for reading or writing
 */
FileManager::~FileManager() {
    closeFile();
}

/**
 * Close the file depending on the specified mode
 */
void FileManager::closeFile() {
    if (m_open_mode == OpenMode::READ) {
        m_in_file.close();
    } else if (m_open_mode == OpenMode::WRITE) {
        m_out_file.close();
    }
}

/**
 * Open a file in the specified mode and handle exceptions
 * @param file_path The path to the file
 */
void FileManager::openFile(const string &file_path) {
    try {
        // Open file in read mode
        if (m_open_mode == OpenMode::READ) {
            m_in_file.open(file_path, ios::binary);
            if (!m_in_file.is_open()) {
                throw runtime_error("Failed to open file for reading");
            }
            // In read mode the member variables related to file info are initialized
            // using the file size to compute them
            initFileInfo(computeFileSize(file_path));

            // Open file in write mode
        } else if (m_open_mode == OpenMode::WRITE) {
            if (isFilePresent(file_path)) {
                throw runtime_error("File already exists");
            }
            m_out_file.open(file_path, ios::binary);
            if (!m_out_file.is_open()) {
                throw runtime_error("Failed to open file for writing");
            }
        }
    } catch (const exception &e) {
        cerr << "FileManager - Error! " << e.what() << endl;
    }
}

/**
 * Compute the size of the file
 * @param in_file The input file stream
 * @return The size of the file in bytes
 */
streamsize FileManager::computeFileSize(const string& file_path) {
    ifstream in_file(file_path, ios::binary);
    streampos begin = in_file.tellg();
    in_file.seekg(0, ios::end);
    streampos end = in_file.tellg();
    if (end < begin) {
        throw runtime_error("Failed to determine file size");
    }
    return end - begin;
}

/**
 * Initialize file information (size, number of chunks, last chunk size)
 * @param file_size The size of the file in bytes
 */
void FileManager::initFileInfo(streamsize file_size) {
    m_file_size = file_size;
    m_chunks_num = ceil((double) m_file_size / (double) Config::CHUNK_SIZE);
    if (m_file_size % Config::CHUNK_SIZE != 0) {
        m_last_chunk_size = m_file_size % Config::CHUNK_SIZE;
    } else {
        m_last_chunk_size = Config::CHUNK_SIZE;
    }
}

/**
 * Retrieve a comma-separated string containing the names of files in the specified directory path.
 *
 * @param path The path of the directory from which to retrieve the list of files.
 * @return A string containing the names of files in the specified directory, separated by commas.
 *         An empty string is returned in case of an error.
 * @throws std::invalid_argument If the provided path does not exist.
 */
string FileManager::getFilesList(const string& path) {
    try {
        // Check if the path exists
        if (!filesystem::exists(path)) {
            throw invalid_argument("Path does not exist.");
        }
        string filesString;
        // Iterate over the files in the specified path
        for (const auto& entry : filesystem::directory_iterator(path)) {
            filesString += entry.path().filename().string() + ",";
        }
        // Remove the trailing "," if there are any files
        if (!filesString.empty()) {
            filesString.pop_back();
        }
        return filesString;
    } catch (const exception& e) {
        cerr << "FileManager - Error! " << e.what() << endl;
        return "error";
    }
}

/**
 * Get the size of the file
 * @return The size of the file in bytes
 */
streamsize FileManager::getFileSize() const {
    return m_file_size;
}

/**
 * Get the number of chunks the file is divided into
 * @return The number of chunks
 */
streamsize FileManager::getChunksNum() const {
    return m_chunks_num;
}

/**
 * Get the size of the last chunk of the file
 * @return The size of the last chunk in bytes
 */
streamsize FileManager::getLastChunkSize() const {
    return m_last_chunk_size;
}

/**
 * Read a chunk of data from the file
 * @param buffer The buffer to store the read data
 * @param size The size of the buffer
 * @return 0 on success, -1 on failure
 */
int FileManager::readChunk(uint8_t *buffer, streamsize size) {
    if (m_open_mode == READ) {
        m_in_file.read((char*)buffer, size);
    } else {
        cerr << "FileManager - Error while reading chunk" << endl;
        return -1;
    }
    return 0;
}

/**
 * Write a chunk of data to the file
 * @param buffer The buffer containing the data to be written
 * @param size The size of the data to be written
 * @return 0 on success, -1 on failure
 */
int FileManager::writeChunk(uint8_t *buffer, streamsize size) {
    if (m_open_mode == WRITE) {
        m_out_file.write((char*)buffer, size);
    } else {
        cerr << "FileManager - Error while writing chunk" << endl;
        return -1;
    }
    return 0;
}

/**
 * Check if a file is present at the specified path
 * @param file_path The path to the file
 * @return True if the file is present, false otherwise
 */
bool FileManager::isFilePresent(const string &file_path) {
    ifstream in_data(file_path, ios::binary);
    return in_data.is_open();
}

/**
 * Check if a string is valid based on certain criteria
 * @param input_string The string to be validated
 * @return True if the string is valid, false otherwise
 */
bool FileManager::isStringValid(const string &input_string) {
    const char whitelist[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_-.@";
    // Check if input string is composed only by characters in the whitelist
    if (strspn(input_string.c_str(), whitelist) < input_string.length()) {
        cout << "FileManager - Error! Characters not allowed" << endl;
        cout << "Allowed characters are: ";
        // Print each character in the whitelist
        for (const char *ptr = whitelist; *ptr; ptr++) {
            cout << *ptr;
        }
        cout << endl;
        return false;
    }
    if (input_string.empty()) {
        cout << "FileManager - Error! The string cannot be empty" << endl;
        return false;
    }
    if (input_string.length() >= Config::FILE_NAME_LEN) {
        cout << "FileManager - Error! The string is too long. Maximum allowed length is "
             << (int) Config::FILE_NAME_LEN - 1 << " characters" << endl;
        return false;
    }
    if (input_string == "." || input_string == "..") {
        cout << "FileManager - Error! Invalid string. Reserved names not allowed" << endl;
        return false;
    }
    return true;
}

/**
 * Get a valid numeric code from the user within the specified range.
 *
 * @param lowerBound The lower bound of the valid range.
 * @param upperBound The upper bound of the valid range.
 * @return The valid numeric code entered by the user.
 */
int FileManager::getValidCode(int lowerBound, int upperBound) {
    string input;
    int userInput;

    // Keep prompting the user until a valid input is provided
    while (true) {
        cout << "Insert operation code: ";
        getline(cin, input);

        // Use stringstream to convert the input to an integer
        stringstream ss(input);
        // Check if the input is a valid integer and doesn't have trailing characters
        if (ss >> userInput && ss.eof()) {
            // Check if the input is within the valid range
            if (userInput >= lowerBound && userInput <= upperBound) {
                break; // Valid input, exit the loop
            }
        }
        cout << "Invalid input. Please enter a value between "
        << lowerBound << " and " << upperBound << "." << endl;
    }
    return userInput;
}



