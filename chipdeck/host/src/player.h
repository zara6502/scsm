// Player: плейлист, сессии движков, состояния воспроизведения.
#pragma once

#include "audio.h"
#include "engine_manager.h"

namespace host {

// Конец трека: Player постит это сообщение в notify-окно из audio-потока.
constexpr UINT kChipMsgTrackEnd = WM_APP + 2;

enum class PlayState { Stopped, Playing, Paused };

struct PlaylistItem {
    std::wstring path;
    int engine_idx = -1;      // индекс в EngineManager
    std::string title, author, format;
    uint32_t tracks = 0;
    uint32_t duration_ms = 0; // 0 = неизвестна
};

class Player {
public:
    // notify_hwnd — окно, куда постится WM_APP_TRACKEND при конце трека.
    bool init(HWND notify_hwnd, EngineManager* em);
    void shutdown();

    // Возвращает индекс добавленного элемента или -1.
    int add_file(const std::wstring& path);
    bool play(int item_idx, uint32_t track = 0);
    void toggle_pause();
    void stop();
    void next();
    void prev();

    PlayState state() const;
    const PlaylistItem* current() const {
        return (cur_ >= 0 && cur_ < int(items_.size())) ? &items_[cur_]
                                                        : nullptr;
    }
    uint32_t current_track() const { return cur_track_; }
    size_t item_count() const { return items_.size(); }
    const PlaylistItem& item(size_t i) const { return items_[i]; }
    void remove(size_t idx); // нельзя удалять текущий играющий

    // Текущая позиция (мс); длительность текущего трека (0 = неизвестна).
    uint32_t position_ms() const;
    uint32_t duration_ms() const;
    uint32_t sample_rate() const { return rate_; }

private:
    void close_session_locked();
    // Читает метаданные трека из открытой сессии.
    void read_meta_locked(const EngineModule& eng, ChipHandle s,
                          uint32_t track, PlaylistItem& item);

    EngineManager* em_ = nullptr;
    WasapiOutput audio_;
    mutable std::mutex mtx_;

    std::vector<PlaylistItem> items_;
    int cur_ = -1;
    uint32_t cur_track_ = 0;
    PlayState state_ = PlayState::Stopped;

    ChipHandle session_ = nullptr;
    const EngineModule* eng_ = nullptr;
    uint32_t rate_ = 48000;
    uint64_t pos_frames_ = 0;   // под mtx_ (мутирует в рендере)
    HWND notify_ = nullptr;
};

} // namespace host
