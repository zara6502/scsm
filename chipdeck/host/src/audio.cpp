#include "audio.h"

#include <cstring>

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT (не линкуем ksguid ради одного GUID):
// {00000003-0000-0010-8000-00AA00389B71}
static const GUID kFloatSubFormat = {
    0x00000003, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

namespace host {

bool WasapiOutput::init() {
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) return false;

    IMMDevice* device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();
    if (FAILED(hr)) return false;

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client_));
    device->Release();
    if (FAILED(hr)) return false;

    hr = client_->GetMixFormat(&wfx_);
    if (FAILED(hr)) return false;

    rate_ = wfx_->nSamplesPerSec;

    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                             200 * 10000, // 200 мс буфер
                             0, wfx_, nullptr);
    if (FAILED(hr)) return false;

    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    client_->SetEventHandle(event_);
    hr = client_->GetService(__uuidof(IAudioRenderClient),
                             reinterpret_cast<void**>(&render_));
    if (FAILED(hr)) return false;

    client_->GetBufferSize(&buffer_frames_);
    return true;
}

void WasapiOutput::shutdown() {
    stop();
    if (render_) render_->Release();
    if (client_) client_->Release();
    if (wfx_) CoTaskMemFree(wfx_);
    if (event_) CloseHandle(event_);
    render_ = nullptr;
    client_ = nullptr;
    wfx_ = nullptr;
    event_ = nullptr;
}

bool WasapiOutput::start(FillFn fill) {
    if (!client_ || !render_) return false;
    {
        std::lock_guard<std::mutex> lk(thread_mtx_);
        if (running_) return true;
        fill_ = std::move(fill);
        running_ = true;
    }
    // Предзаполнение буфера тишиной и старт.
    BYTE* p = nullptr;
    if (SUCCEEDED(render_->GetBuffer(buffer_frames_, &p))) {
        std::memset(p, 0, size_t(buffer_frames_) * wfx_->nBlockAlign);
        render_->ReleaseBuffer(buffer_frames_, 0);
    }
    client_->Start();
    thread_ = std::thread(&WasapiOutput::render_loop, this);
    return true;
}

void WasapiOutput::stop() {
    {
        std::lock_guard<std::mutex> lk(thread_mtx_);
        if (!running_) return;
        running_ = false;
    }
    if (event_) SetEvent(event_);
    if (thread_.joinable()) thread_.join();
    if (client_) client_->Stop();
    fill_ = nullptr;
}

void WasapiOutput::render_loop() {
    const bool is_float =
        wfx_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (wfx_->wFormatTag == WAVE_FORMAT_EXTENSIBLE && [&] {
            auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx_);
            return IsEqualGUID(x->SubFormat, kFloatSubFormat);
        }());
    const int bits = int(wfx_->wBitsPerSample);

    while (true) {
        const DWORD w = WaitForSingleObject(event_, 2000);
        {
            std::lock_guard<std::mutex> lk(thread_mtx_);
            if (!running_) return;
        }
        if (w != WAIT_OBJECT_0) continue;

        UINT32 padding = 0;
        if (FAILED(client_->GetCurrentPadding(&padding))) continue;
        UINT32 avail = buffer_frames_ - padding;
        if (avail == 0) continue;

        BYTE* p = nullptr;
        if (FAILED(render_->GetBuffer(avail, &p))) continue;
        if (is_float && bits == 32) {
            std::vector<int16_t> pcm(size_t(avail) * 2);
            fill_(pcm.data(), avail);
            float* out = reinterpret_cast<float*>(p);
            for (UINT32 i = 0; i < UINT32(pcm.size()); ++i)
                out[i] = float(pcm[i]) * (1.0f / 32768.0f);
        } else if (is_float && bits == 16) {
            fill_(reinterpret_cast<int16_t*>(p), avail);
        } else {
            std::memset(p, 0, size_t(avail) * wfx_->nBlockAlign);
        }
        render_->ReleaseBuffer(avail, 0);
    }
}

} // namespace host
