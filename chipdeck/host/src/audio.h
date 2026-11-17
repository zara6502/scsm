// WASAPI-вывод: event-driven рендер-поток, конверсия int16 -> float32.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace host {

class WasapiOutput {
public:
    using FillFn = std::function<void(int16_t* out, uint32_t frames)>;

    // Инициализация устройства по умолчанию; возвращает частоту микса.
    // Вызывать после CoInitializeEx(MTA).
    bool init();
    void shutdown();

    // Запускает рендер-поток; fill вызывается постоянно (передаёт тишину,
    // если воспроизведение остановлено).
    bool start(FillFn fill);
    void stop();

    uint32_t rate() const { return rate_; }

private:
    void render_loop();

    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    WAVEFORMATEX* wfx_ = nullptr;
    UINT32 buffer_frames_ = 0;
    uint32_t rate_ = 48000;
    HANDLE event_ = nullptr;
    std::thread thread_;
    std::mutex thread_mtx_;
    FillFn fill_;
    bool running_ = false;
};

} // namespace host
