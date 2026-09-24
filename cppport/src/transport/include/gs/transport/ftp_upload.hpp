#pragma once

// Uploads to a networked grblHAL's SD card over FTP (server/lib/GrblHALFTP.js,
// which uses basic-ftp): log in, binary mode, then per file a passive data
// connection, STOR and the bytes; progress by the bytes sent of each file.
//
// Threading: as AsioLink - called on the owner thread, the transfer runs on
// a thread of its own and the callbacks come back through the Dispatcher;
// none after the uploader is destroyed.

#include "gs/transport/asio_link.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gs::transport {

struct FtpOptions {
    std::string host;
    std::uint16_t port = 21;  // grblHAL's $308
    std::string user = "grblHAL";
    std::string password = "grblHAL";
    std::int64_t timeoutMs = 10000;  // for each exchange
};

struct FtpFile {
    std::string name;
    std::string data;
};

class FtpUploader {
public:
    struct Callbacks {
        std::function<void()> started;
        std::function<void(int percent)> progress;  // of the file being sent
        std::function<void()> completed;
        std::function<void(const std::string& message)> failed;
    };

    explicit FtpUploader(Dispatcher dispatch);
    ~FtpUploader();
    FtpUploader(const FtpUploader&) = delete;
    FtpUploader& operator=(const FtpUploader&) = delete;

    // One upload at a time: false (and nothing called) while one runs.
    bool upload(FtpOptions options, std::vector<FtpFile> files, Callbacks callbacks);
    bool active() const noexcept { return active_.load(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> active_{false};
};

}  // namespace gs::transport
