#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class MacFrameWorker
{
public:
    struct Interrupted {};

    MacFrameWorker() = default;
    ~MacFrameWorker();
    MacFrameWorker(const MacFrameWorker&) = delete;
    MacFrameWorker& operator=(const MacFrameWorker&) = delete;

    std::vector<float> Process(std::uint32_t width, std::uint32_t height,
                               const std::vector<float>& rgb,
                               const std::function<bool()>& interrupted = {});

private:
    void Start();
    void Stop() noexcept;

    int socket_ = -1;
    int child_ = -1;
};
