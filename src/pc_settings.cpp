#include "battletanx/pc_settings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace battletanx {
namespace {
std::optional<std::filesystem::path> data_override;

std::optional<std::string> environment(const char* name) {
#if defined(_WIN32)
    char* value=nullptr;
    std::size_t size{};
    if (_dupenv_s(&value,&size,name)!=0 || value==nullptr) return std::nullopt;
    std::string result{value};
    std::free(value);
    return result.empty() ? std::nullopt : std::optional<std::string>{std::move(result)};
#else
    const char* value=std::getenv(name);
    return value==nullptr || *value=='\0' ? std::nullopt : std::optional<std::string>{value};
#endif
}

std::string_view trim(std::string_view value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if (first==std::string_view::npos) return {};
    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1U);
}

template<typename T> bool number(std::string_view value,T& output) {
    value=trim(value);
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),output);
    return parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size();
}

template<> bool number<float>(std::string_view value,float& output) {
    try {
        std::size_t used{};
        const std::string text{trim(value)};
        const float parsed=std::stof(text,&used);
        if (used!=text.size() || !std::isfinite(parsed)) return false;
        output=parsed;
        return true;
    } catch (const std::exception&) { return false; }
}
} // namespace

PcSettings PcSettings::load(const std::filesystem::path& path) {
    PcSettings settings;
    std::ifstream stream{path};
    std::string line;
    while (std::getline(stream,line)) {
        const std::string_view clean=trim(line);
        if (clean.empty() || clean.front()=='#' || clean.front()==';') continue;
        const auto separator=clean.find('=');
        if (separator==std::string_view::npos) continue;
        const auto key=trim(clean.substr(0U,separator));
        const auto value=trim(clean.substr(separator+1U));
        if (key=="version") number(value,settings.version);
        else if (key=="video.window_mode") {
            if (value=="borderless") settings.window_mode=WindowMode::borderless;
            else if (value=="windowed") settings.window_mode=WindowMode::windowed;
        } else if (key=="video.window_width") number(value,settings.window_width);
        else if (key=="video.window_height") number(value,settings.window_height);
        else if (key=="audio.master_volume") number(value,settings.master_volume);
        else if (key=="audio.music_volume") number(value,settings.music_volume);
        else if (key=="audio.effects_volume") number(value,settings.effects_volume);
        else if (key=="controller.deadzone") number(value,settings.controller_deadzone);
        else if (key=="controller.sensitivity") number(value,settings.controller_sensitivity);
        else if (key=="controller.rumble") number(value,settings.rumble_strength);
        else if (key=="rom.last_path") settings.last_rom_path=std::string{value};
    }
    settings.validate();
    return settings;
}

void PcSettings::save(const std::filesystem::path& path) const {
    PcSettings clean=*this;
    clean.validate();
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary=path;
    temporary += ".tmp";
    std::ofstream stream{temporary,std::ios::trunc};
    if (!stream) throw std::runtime_error("unable to create PC settings file");
    stream << "# BattleTanx PC settings\nversion=" << current_version
           << "\nvideo.window_mode=" << (clean.window_mode==WindowMode::borderless ? "borderless" : "windowed")
           << "\nvideo.window_width=" << clean.window_width
           << "\nvideo.window_height=" << clean.window_height
           << "\naudio.master_volume=" << clean.master_volume
           << "\naudio.music_volume=" << clean.music_volume
           << "\naudio.effects_volume=" << clean.effects_volume
           << "\ncontroller.deadzone=" << clean.controller_deadzone
           << "\ncontroller.sensitivity=" << clean.controller_sensitivity
           << "\ncontroller.rumble=" << clean.rumble_strength
           << "\nrom.last_path=" << clean.last_rom_path << '\n';
    stream.close();
    if (!stream) throw std::runtime_error("unable to write PC settings file");
    std::error_code error;
    std::filesystem::rename(temporary,path,error);
    if (error) {
        std::filesystem::remove(path,error);
        error.clear();
        std::filesystem::rename(temporary,path,error);
    }
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("unable to replace PC settings file: "+error.message());
    }
}

void PcSettings::validate() noexcept {
    version=current_version;
    window_width=std::clamp(window_width,640,7680);
    window_height=std::clamp(window_height,480,4320);
    const auto finite=[](float value,float fallback) { return std::isfinite(value) ? value : fallback; };
    master_volume=std::clamp(finite(master_volume,1.0F),0.0F,1.0F);
    music_volume=std::clamp(finite(music_volume,1.0F),0.0F,1.0F);
    effects_volume=std::clamp(finite(effects_volume,1.0F),0.0F,1.0F);
    controller_deadzone=std::clamp(finite(controller_deadzone,6000.0F/32767.0F),0.0F,0.95F);
    controller_sensitivity=std::clamp(finite(controller_sensitivity,1.0F),0.1F,3.0F);
    rumble_strength=std::clamp(finite(rumble_strength,1.0F),0.0F,1.0F);
}

void PcSettings::remember_rom(const std::filesystem::path& path) {
    std::error_code error;
    auto normalized=std::filesystem::absolute(path,error);
    if (error) normalized=path;
    const auto encoded=normalized.lexically_normal().generic_u8string();
    last_rom_path.assign(reinterpret_cast<const char*>(encoded.data()),encoded.size());
}

void configure_user_data_directory(const std::filesystem::path& executable_path) {
    data_override.reset();
    std::error_code error;
    auto executable=std::filesystem::absolute(executable_path,error);
    if (error) executable=executable_path;
    const auto directory=executable.parent_path();
    if (!directory.empty() && std::filesystem::is_regular_file(directory/"portable.txt",error))
        data_override=directory/"userdata";
}

std::filesystem::path user_data_directory() {
    if (const auto explicit_path=environment("BATTLETANX_DATA_DIR")) return *explicit_path;
    if (data_override) return *data_override;
#if defined(_WIN32)
    if (const auto local=environment("LOCALAPPDATA")) return std::filesystem::path{*local}/"BattleTanx";
    if (const auto roaming=environment("APPDATA")) return std::filesystem::path{*roaming}/"BattleTanx";
#else
    if (const auto xdg=environment("XDG_DATA_HOME")) return std::filesystem::path{*xdg}/"BattleTanx";
    if (const auto home=environment("HOME")) return std::filesystem::path{*home}/".local"/"share"/"BattleTanx";
#endif
    return std::filesystem::temp_directory_path()/"BattleTanx";
}

std::filesystem::path path_from_utf8(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte:value) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path{encoded};
}

} // namespace battletanx
