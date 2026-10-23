#pragma once

#include <filesystem>
#include <string>

namespace battletanx {

enum class WindowMode { windowed,borderless };

struct PcSettings {
    static constexpr int current_version=1;
    int version{current_version};
    WindowMode window_mode{WindowMode::windowed};
    int window_width{960};
    int window_height{720};
    float master_volume{1.0F};
    float music_volume{1.0F};
    float effects_volume{1.0F};
    float controller_deadzone{6000.0F/32767.0F};
    float controller_sensitivity{1.0F};
    float rumble_strength{1.0F};
    std::string last_rom_path;

    static PcSettings load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    void validate() noexcept;
    void remember_rom(const std::filesystem::path& path);
};

void configure_user_data_directory(const std::filesystem::path& executable_path);
[[nodiscard]] std::filesystem::path user_data_directory();
[[nodiscard]] std::filesystem::path path_from_utf8(const std::string& value);

} // namespace battletanx
