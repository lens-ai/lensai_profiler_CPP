#include "http_uploader.h"
#include <fstream>
#include <vector>
#include <ctime>
#include <curl/curl.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

HttpUploader::HttpUploader(http_uploader_data_t &http_uploader_data)
    : http_uploader_data_(http_uploader_data) {
}

void HttpUploader::StartUpload() {
    upload_thread_ = std::thread(&HttpUploader::UploadLoop, this);
}

void HttpUploader::UploadLoop() {
    while (true) {
        do {

            if (exitUploadLoop.load()) {
              pthread_exit(nullptr);// Thread termination condition
            }

            std::unique_lock<std::mutex> lock(upload_mutex_);

            if (exitUploadLoop.load()) {
                pthread_exit(nullptr);// Thread termination condition
            }

            uploadFolder();

        }while(0); //scope of queue_mutex_

        for (int i = 0; i < http_uploader_data_.interval; i++) {
            if (exitUploadLoop.load()) {
                pthread_exit(nullptr);// Thread termination condition
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void HttpUploader::StopUpload(void) {
    if (upload_thread_.joinable()) {
        exitUploadLoop.store(true);
        upload_thread_.join();
    }
}

bool HttpUploader::uploadFolder() {
    // Get current time as time_point
    auto now = std::chrono::system_clock::now();
    // Convert to time_t
    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    TarGzCreator tarGzCreator;

    // Step 1: Create tar file
    std::vector<std::string> folders = { http_uploader_data_.folderPath };
    std::vector<std::string> files = tarGzCreator.collectFilesFromFolders(folders);
    std::string tarFilePath = "archive.tar";
    std::string gzFilePath = "archive.tar.gz";

    if (!tarGzCreator.createTar(tarFilePath, files)) {
        std::cerr << "Failed to create tar file." << std::endl;
        return false;
    }

    // Step 2: Compress to tar.gz
    if (!tarGzCreator.compressToGz(tarFilePath, gzFilePath)) {
        std::cerr << "Failed to compress tar file to gz." << std::endl;
        return false;
    }

    // Step 3: Empty the folder
    if (!tarGzCreator.emptyFolder(http_uploader_data_.folderPath) && http_uploader_data_.deletedata) {
        std::cerr << "Failed to empty the folder." << std::endl;
        return false;
    }

    // Step 4: Upload the file
    if (!postFile(gzFilePath, http_uploader_data_.sensorId, timestamp, http_uploader_data_.fileType)) {
        std::cerr << "Failed to upload gz file." << std::endl;
        return false;
    }

    // Step 5: Delete the gz file
    if (fs::exists(gzFilePath)) {
        fs::remove(gzFilePath);
    }

    // Step 6: Delete the tar file
    if (fs::exists(tarFilePath)) {
        fs::remove(tarFilePath);
    }

    return true;
}

bool HttpUploader::postFile(const std::string& filePath, const std::string& sensorId, time_t timestamp, const std::string& fileType) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    CURLcode res;
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        curl_easy_cleanup(curl);
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> fileBuffer(fileSize);
    file.read(fileBuffer.data(), fileSize);

    curl_easy_setopt(curl, CURLOPT_URL, http_uploader_data_.endpointUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + http_uploader_data_.token).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    struct curl_httppost* formpost = nullptr;
    struct curl_httppost* lastptr = nullptr;

    // Adding metadata and the file to the form
    curl_formadd(&formpost, &lastptr,
        CURLFORM_COPYNAME, "sensor_id",
        CURLFORM_COPYCONTENTS, sensorId.c_str(),
        CURLFORM_END);

    curl_formadd(&formpost, &lastptr,
        CURLFORM_COPYNAME, "timestamp",
        CURLFORM_COPYCONTENTS, std::to_string(timestamp).c_str(),
        CURLFORM_END);

    curl_formadd(&formpost, &lastptr,
        CURLFORM_COPYNAME, "file_type",
        CURLFORM_COPYCONTENTS, fileType.c_str(),
        CURLFORM_END);

    curl_formadd(&formpost, &lastptr,
        CURLFORM_COPYNAME, "file",
        CURLFORM_BUFFER, filePath.c_str(),
        CURLFORM_BUFFERLENGTH, fileBuffer.size(),
        CURLFORM_BUFFERPTR, fileBuffer.data(),
        CURLFORM_CONTENTTYPE, "application/gzip", // Specify the content type as gzip
        CURLFORM_END);

    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

    res = curl_easy_perform(curl);

    curl_formfree(formpost);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

