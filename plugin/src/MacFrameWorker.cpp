#include "MacFrameWorker.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto kRequestTimeout = 150s;
constexpr std::uint32_t kMaxErrorSize = 65536;

class Descriptor
{
public:
    explicit Descriptor(int value) : value_(value) {}
    ~Descriptor() { if (value_ >= 0) close(value_); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int Get() const { return value_; }
    int Release() { const int value = value_; value_ = -1; return value; }

private:
    int value_;
};

void CheckPosix(int error, const char* operation)
{
    if (error != 0)
        throw std::system_error(error, std::generic_category(), operation);
}

bool ReapWorker(pid_t pid)
{
    int status = 0;
    const auto until = Clock::now() + 250ms;
    do {
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result > 0 || (result < 0 && errno != EINTR))
            return true;
        std::this_thread::sleep_for(10ms);
    } while (Clock::now() < until);
    return false;
}

std::filesystem::path WorkerDirectory()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        throw std::runtime_error("Could not locate the CorridorKey runtime config in HOME.");
    return std::filesystem::path(home) / "Library/Application Support/CorridorKeyAVX";
}

std::filesystem::path WorkerScript()
{
    Dl_info module{};
    if (!dladdr(reinterpret_cast<const void*>(&WorkerScript), &module) || !module.dli_fname)
        throw std::runtime_error("Could not locate the CorridorKey AVX bundle.");
    return std::filesystem::path(module.dli_fname).parent_path().parent_path()
        / "Resources/macos_worker.py";
}

void Transfer(int fd, void* data, std::size_t size, bool sendData, Clock::time_point deadline,
              const std::function<bool()>& interrupted)
{
    auto* position = static_cast<unsigned char*>(data);
    while (size > 0) {
        if (interrupted && interrupted())
            throw MacFrameWorker::Interrupted{};
        const auto milliseconds =
            std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now());
        if (milliseconds <= 0ms)
            throw std::runtime_error("CorridorKey worker timed out.");
        pollfd descriptor{fd, static_cast<short>(sendData ? POLLOUT : POLLIN), 0};
        const auto wait = interrupted ? std::min(milliseconds, 100ms) : milliseconds;
        const int ready = poll(&descriptor, 1, static_cast<int>(wait.count()));
        if (ready == 0)
            continue;
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            CheckPosix(errno, "Poll CorridorKey worker");
        }
        const ssize_t transferred = sendData ? send(fd, position, size, 0)
                                             : recv(fd, position, size, 0);
        if (transferred == 0)
            throw std::runtime_error("CorridorKey worker closed the connection.");
        if (transferred < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            CheckPosix(errno, "Transfer CorridorKey frame");
        }
        position += transferred;
        size -= static_cast<std::size_t>(transferred);
    }
}

}

MacFrameWorker::~MacFrameWorker()
{
    Stop();
}

void MacFrameWorker::Start()
{
    const auto directory = WorkerDirectory();
    const auto configPath = directory / "worker.conf";
    std::ifstream config(configPath);
    std::string python, source, assets;
    if (!std::getline(config, python) || !std::getline(config, source) ||
        !std::getline(config, assets) || !std::filesystem::path(python).is_absolute() ||
        !std::filesystem::path(source).is_absolute() ||
        !std::filesystem::path(assets).is_absolute()) {
        throw std::runtime_error("Expected Python, source and model asset paths in " + configPath.string());
    }

    auto script = WorkerScript().string();
    const auto logPath = directory / "worker.log";
    Descriptor log(open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600));
    if (log.Get() < 0)
        CheckPosix(errno, "Open CorridorKey worker log");
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0)
        CheckPosix(errno, "Create CorridorKey worker socket");
    Descriptor parent(sockets[0]), child(sockets[1]);
    const int enabled = 1;
    if (setsockopt(parent.Get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0 ||
        fcntl(parent.Get(), F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(parent.Get(), F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(child.Get(), F_SETFD, FD_CLOEXEC) < 0)
        CheckPosix(errno, "Configure CorridorKey worker socket");

    posix_spawn_file_actions_t actions;
    CheckPosix(posix_spawn_file_actions_init(&actions), "Initialize worker file actions");
    pid_t pid = -1;
    try {
        CheckPosix(posix_spawn_file_actions_adddup2(&actions, child.Get(), STDIN_FILENO), "Set worker input");
        CheckPosix(posix_spawn_file_actions_adddup2(&actions, child.Get(), STDOUT_FILENO), "Set worker output");
        CheckPosix(posix_spawn_file_actions_adddup2(&actions, log.Get(), STDERR_FILENO), "Set worker log");
        CheckPosix(posix_spawn_file_actions_addclose(&actions, parent.Get()), "Close parent worker socket");
        CheckPosix(posix_spawn_file_actions_addclose(&actions, child.Get()), "Close inherited worker socket");
        char unbuffered[] = "-u";
        char sourceOption[] = "--source-root";
        char assetsOption[] = "--asset-root";
        char* const arguments[] = {
            python.data(), unbuffered, script.data(), sourceOption, source.data(),
            assetsOption, assets.data(), nullptr,
        };
        CheckPosix(posix_spawn(&pid, python.c_str(), &actions, nullptr, arguments, environ), "Start CorridorKey worker");
    } catch (...) {
        posix_spawn_file_actions_destroy(&actions);
        throw;
    }
    posix_spawn_file_actions_destroy(&actions);
    socket_ = parent.Release();
    child_ = pid;
}

void MacFrameWorker::Stop() noexcept
{
    if (socket_ >= 0) {
        close(socket_);
        socket_ = -1;
    }
    if (child_ < 0)
        return;
    if (!ReapWorker(child_)) {
        kill(child_, SIGKILL);
        ReapWorker(child_);
    }
    child_ = -1;
}

std::vector<float> MacFrameWorker::Process(std::uint32_t width, std::uint32_t height,
                                          const std::vector<float>& rgb,
                                          const std::function<bool()>& interrupted)
{
    static_assert(sizeof(float) == 4, "The worker protocol requires float32.");
    if (width == 0 || height == 0 || width > 1920 || height > 1080 ||
        rgb.size() != static_cast<std::size_t>(width) * height * 3)
        throw std::invalid_argument("CorridorKey received an invalid RGB frame.");

    std::string workerError;
    std::vector<float> rgba;
    try {
        if (socket_ < 0)
            Start();
        const auto deadline = Clock::now() + kRequestTimeout;
        std::uint32_t header[2] = {width, height};
        Transfer(socket_, header, sizeof(header), true, deadline, interrupted);
        Transfer(socket_, const_cast<float*>(rgb.data()), rgb.size() * sizeof(float), true,
                 deadline, interrupted);
        Transfer(socket_, header, sizeof(header), false, deadline, interrupted);

        const auto samples = static_cast<std::size_t>(width) * height * 4;
        if (header[0] == 0 && header[1] == samples * sizeof(float)) {
            rgba.resize(samples);
            Transfer(socket_, rgba.data(), header[1], false, deadline, interrupted);
        } else if (header[0] == 1 && header[1] > 0 && header[1] <= kMaxErrorSize) {
            workerError.resize(header[1]);
            Transfer(socket_, workerError.data(), header[1], false, deadline, interrupted);
        } else {
            throw std::runtime_error("Invalid CorridorKey worker response.");
        }
    } catch (...) {
        Stop();
        throw;
    }
    if (!workerError.empty()) {
        Stop();
        throw std::runtime_error(workerError);
    }
    return rgba;
}
