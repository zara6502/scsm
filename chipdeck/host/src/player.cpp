#include "player.h"

#include <cmath>
#include <cstring>

namespace host {

// конец трека постится через host::kChipMsgTrackEnd (player.h)

bool Player::init(HWND notify_hwnd, EngineManager* em) {
    em_ = em;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    notify_ = notify_hwnd;

    if (!audio_.init()) return false;
    rate_ = audio_.rate();

    // Постоянный рендер-поток: тишину или трек, по состоянию.
    return audio_.start([this](int16_t* out, uint32_t frames) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ == PlayState::Playing && session_ && eng_) {
            const uint32_t got = eng_->render_i16(session_, out, frames);
            pos_frames_ += got;
            if (got < frames) {
                // конец трека: остаток — тишина, уведомляем UI-поток
                std::memset(out + size_t(got) * 2, 0,
                            size_t(frames - got) * 2 * sizeof(int16_t));
                if (notify_)
                    PostMessageW(notify_, kChipMsgTrackEnd, 0, 0);
            }
        } else {
            std::memset(out, 0, size_t(frames) * 2 * sizeof(int16_t));
        }
    });
}

void Player::shutdown() {
    audio_.stop();
    std::lock_guard<std::mutex> lk(mtx_);
    close_session_locked();
    CoUninitialize();
}

void Player::close_session_locked() {
    if (session_ && eng_) eng_->close_fn(session_);
    session_ = nullptr;
    eng_ = nullptr;
    pos_frames_ = 0;
}

void Player::read_meta_locked(const EngineModule& eng, ChipHandle s,
                              uint32_t track, PlaylistItem& item) {
    ChipTrackInfo ti{};
    ti.struct_size = sizeof(ti);
    if (eng.track_info(s, track, &ti)) {
        item.title = ti.title;
        item.author = ti.author;
        item.format = ti.format;
        item.duration_ms = ti.duration_ms;
    }
}

int Player::add_file(const std::wstring& path) {
    std::vector<uint8_t> bytes;
    if (!read_file_bytes(path, bytes) || bytes.empty()) return -1;

    std::lock_guard<std::mutex> lk(mtx_);
    const EngineModule* eng = em_->find_for_content(bytes.data(), bytes.size());
    if (!eng) return -1;

    ChipHandle s = nullptr;
    ChipError err{};
    if (!eng->open_fn(bytes.data(), bytes.size(), &s, &err)) return -1;

    PlaylistItem item;
    item.path = path;
    item.engine_idx = int(eng - &em_->at(0));
    eng->track_count(s, &item.tracks);

    // Метаданные первого подтюна как «лицо» записи; полный список
    // подтюнов раскрывается при воспроизведении.
    read_meta_locked(*eng, s, 0, item);
    eng->close_fn(s);

    if (item.tracks == 0) item.tracks = 1;
    items_.push_back(std::move(item));
    return int(items_.size()) - 1;
}

bool Player::play(int item_idx, uint32_t track) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (item_idx < 0 || item_idx >= int(items_.size())) return false;

    PlaylistItem& item = items_[size_t(item_idx)];
    std::vector<uint8_t> bytes;
    if (!read_file_bytes(item.path, bytes)) return false;

    const EngineModule& eng = em_->at(size_t(item.engine_idx));

    ChipHandle s = nullptr;
    ChipError err{};
    if (!eng.open_fn(bytes.data(), bytes.size(), &s, &err)) return false;

    if (!eng.select_fn(s, track, rate_, &err)) {
        eng.close_fn(s);
        return false;
    }

    close_session_locked();
    session_ = s;
    eng_ = &eng;
    cur_ = item_idx;
    cur_track_ = track;
    pos_frames_ = 0;
    state_ = PlayState::Playing;
    return true;
}

void Player::toggle_pause() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_ == PlayState::Playing) state_ = PlayState::Paused;
    else if (state_ == PlayState::Paused && session_) state_ = PlayState::Playing;
}

void Player::stop() {
    std::lock_guard<std::mutex> lk(mtx_);
    close_session_locked();
    state_ = PlayState::Stopped;
}

void Player::next() {
    if (items_.empty()) return;
    const int n = int(items_.size());
    const int base = cur_ < 0 ? 0 : cur_;
    for (int k = 1; k <= n; ++k) { // следующий существующий, с заворотом
        if (play((base + k) % n)) return;
    }
}

void Player::prev() {
    if (items_.empty()) return;
    const int n = int(items_.size());
    const int base = cur_ < 0 ? 0 : cur_;
    for (int k = 1; k <= n; ++k) {
        if (play((base + n - k) % n)) return;
    }
}

void Player::remove(size_t idx) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (int(idx) == cur_) return; // играющий не трогаем
    if (idx < items_.size()) items_.erase(items_.begin() + long(idx));
    if (int(idx) < cur_) --cur_;
}

PlayState Player::state() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return state_;
}

uint32_t Player::position_ms() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (rate_ == 0) return 0;
    return uint32_t((pos_frames_ / uint64_t(rate_)) * 1000u);
}

uint32_t Player::duration_ms() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (cur_ < 0 || cur_ >= int(items_.size())) return 0;
    return items_[size_t(cur_)].duration_ms;
}

} // namespace host
