#include "battletanx/platform.hpp"
#include "battletanx/host_input.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace battletanx {
namespace {
constexpr std::uint16_t button_a=0x8000U;
constexpr std::uint16_t button_b=0x4000U;
constexpr std::uint16_t button_z=0x2000U;
constexpr std::uint16_t button_start=0x1000U;
constexpr std::uint16_t button_d_up=0x0800U;
constexpr std::uint16_t button_d_down=0x0400U;
constexpr std::uint16_t button_d_left=0x0200U;
constexpr std::uint16_t button_d_right=0x0100U;
constexpr std::uint16_t button_l=0x0020U;
constexpr std::uint16_t button_r=0x0010U;
constexpr std::uint16_t button_c_up=0x0008U;
constexpr std::uint16_t button_c_down=0x0004U;
constexpr std::uint16_t button_c_left=0x0002U;
constexpr std::uint16_t button_c_right=0x0001U;
std::runtime_error sdl_error(std::string_view operation) {
    return std::runtime_error(std::string(operation)+": "+SDL_GetError());
}

class SdlPlatform final : public Platform {
  public:
    explicit SdlPlatform(std::string_view title) {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_HAPTIC)!=0) {
            const auto error=sdl_error("SDL initialization failed");
            SDL_Quit();
            throw error;
        }
        window_=SDL_CreateWindow(std::string(title).c_str(),SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,native_width,native_height,
                                 SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
        if (window_==nullptr) {
            const auto error=sdl_error("window creation failed");
            SDL_Quit();
            throw error;
        }
        renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
        if (renderer_==nullptr) renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);
        if (renderer_==nullptr) {
            const auto error=sdl_error("renderer creation failed");
            SDL_DestroyWindow(window_);
            window_=nullptr;
            SDL_Quit();
            throw error;
        }
        SDL_RenderSetLogicalSize(renderer_,native_width,native_height);
        texture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,native_width,native_height);
        if (texture_==nullptr) {
            const auto error=sdl_error("framebuffer texture creation failed");
            SDL_DestroyRenderer(renderer_);
            SDL_DestroyWindow(window_);
            renderer_=nullptr;
            window_=nullptr;
            SDL_Quit();
            throw error;
        }
        SDL_AudioSpec desired{};
        desired.freq=44100;
        desired.format=AUDIO_S16SYS;
        desired.channels=2U;
        desired.samples=1024U;
        desired.callback=&SdlPlatform::audio_callback;
        desired.userdata=this;
        audio_spec_=desired;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)==0) {
            audio_device_=SDL_OpenAudioDevice(nullptr,0,&desired,&audio_spec_,0);
        }
        // Video and input remain fully usable when the audio subsystem or
        // output device is unavailable. The requested format remains stored
        // so asset validation stays deterministic while playback is muted.
        voices_.reserve(64U);
        if (audio_device_!=0U) SDL_PauseAudioDevice(audio_device_,0);
        for (int index=0; index<SDL_NumJoysticks(); ++index) open_controller(index);
    }

    ~SdlPlatform() override {
        shutdown();
    }

  private:
    void shutdown() noexcept {
        if (audio_device_!=0U) SDL_CloseAudioDevice(audio_device_);
        audio_device_=0U;
        for (auto*& controller : controllers_) {
            if (controller!=nullptr) SDL_GameControllerClose(controller);
            controller=nullptr;
        }
        if (texture_!=nullptr) SDL_DestroyTexture(texture_);
        if (renderer_!=nullptr) SDL_DestroyRenderer(renderer_);
        if (window_!=nullptr) SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    bool pump(std::array<ControllerPad,4>& output) override {
        close_detached_controllers();
        SDL_Event event{};
        while (SDL_PollEvent(&event)!=0) {
            if (event.type==SDL_QUIT) return false;
            if (event.type==SDL_CONTROLLERDEVICEADDED) open_controller(event.cdevice.which);
            if (event.type==SDL_CONTROLLERDEVICEREMOVED) close_controller(event.cdevice.which);
            if (event.type==SDL_KEYDOWN && event.key.repeat==0 &&
                (event.key.keysym.sym==SDLK_F11 ||
                 (event.key.keysym.sym==SDLK_RETURN &&
                  (event.key.keysym.mod&KMOD_ALT)!=0))) {
                const bool fullscreen=(SDL_GetWindowFlags(window_)&SDL_WINDOW_FULLSCREEN_DESKTOP)!=0U;
                SDL_SetWindowFullscreen(window_,fullscreen ? 0U :
                    static_cast<Uint32>(SDL_WINDOW_FULLSCREEN_DESKTOP));
            }
        }
        const Uint8* keys=SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_ESCAPE]!=0) return false;
        output.fill({});
        for (std::size_t port=0; port<controllers_.size(); ++port) {
            if (controllers_[port]!=nullptr && SDL_GameControllerGetAttached(controllers_[port])) {
                output[port]=read_controller(controllers_[port]);
            }
        }
        ControllerPad& keyboard=output[0];
        const int keyboard_x=(keys[SDL_SCANCODE_D]!=0?127:0)-(keys[SDL_SCANCODE_A]!=0?127:0);
        const int keyboard_y=(keys[SDL_SCANCODE_W]!=0?127:0)-(keys[SDL_SCANCODE_S]!=0?127:0);
        if (keyboard_x!=0) keyboard.stick_x=static_cast<std::int8_t>(keyboard_x);
        if (keyboard_y!=0) keyboard.stick_y=static_cast<std::int8_t>(keyboard_y);
        if (keys[SDL_SCANCODE_X]!=0) keyboard.buttons|=button_a;
        if (keys[SDL_SCANCODE_Z]!=0) keyboard.buttons|=button_b;
        if (keys[SDL_SCANCODE_LSHIFT]!=0) keyboard.buttons|=button_z;
        if (keys[SDL_SCANCODE_RETURN]!=0) keyboard.buttons|=button_start;
        if (keys[SDL_SCANCODE_UP]!=0) keyboard.buttons|=button_d_up;
        if (keys[SDL_SCANCODE_DOWN]!=0) keyboard.buttons|=button_d_down;
        if (keys[SDL_SCANCODE_LEFT]!=0) keyboard.buttons|=button_d_left;
        if (keys[SDL_SCANCODE_RIGHT]!=0) keyboard.buttons|=button_d_right;
        if (keys[SDL_SCANCODE_Q]!=0) keyboard.buttons|=button_l;
        if (keys[SDL_SCANCODE_E]!=0) keyboard.buttons|=button_r;
        if (keys[SDL_SCANCODE_I]!=0) keyboard.buttons|=button_c_up;
        if (keys[SDL_SCANCODE_K]!=0) keyboard.buttons|=button_c_down;
        if (keys[SDL_SCANCODE_J]!=0) keyboard.buttons|=button_c_left;
        if (keys[SDL_SCANCODE_L]!=0) keyboard.buttons|=button_c_right;
        return true;
    }

    void present(std::span<const std::uint32_t> pixels,int width,int height) override {
        if (width!=native_width || height!=native_height ||
            pixels.size()!=static_cast<std::size_t>(width*height)) {
            throw std::runtime_error("invalid native framebuffer dimensions");
        }
        if (SDL_UpdateTexture(texture_,nullptr,pixels.data(),width*4)!=0) {
            throw sdl_error("framebuffer upload failed");
        }
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_,texture_,nullptr,nullptr);
        SDL_RenderPresent(renderer_);
    }

    void set_sound_bank(const SoundBank& sounds) override {
        if (sounds.sample_rate!=static_cast<std::uint32_t>(audio_spec_.freq)) {
            throw std::runtime_error("sound bank rate does not match the native audio device");
        }
        if (audio_device_!=0U) SDL_LockAudioDevice(audio_device_);
        sounds_=sounds;
        voices_.clear();
        if (audio_device_!=0U) SDL_UnlockAudioDevice(audio_device_);
    }

    void set_music_track(const MusicTrack& music,float gain) override {
        if (music.sample_rate!=static_cast<std::uint32_t>(audio_spec_.freq) ||
            (music.stereo_samples.size()&1U)!=0U || !std::isfinite(gain)) {
            throw std::runtime_error("music track does not match the native audio device");
        }
        if (audio_device_!=0U) SDL_LockAudioDevice(audio_device_);
        music_=music;
        music_cursor_=0U;
        music_gain_=std::clamp(gain,0.0F,1.0F);
        if (audio_device_!=0U) SDL_UnlockAudioDevice(audio_device_);
    }

    void play_sound(std::uint16_t sound_id,float pan,float gain) override {
        if (audio_device_==0U) return;
        if (sound_id>=sounds_.sounds.size() || !std::isfinite(pan) || !std::isfinite(gain)) return;
        pan=std::clamp(pan,-1.0F,1.0F);
        gain=std::clamp(gain,0.0F,1.0F);
        SDL_LockAudioDevice(audio_device_);
        if (voices_.size()>=64U) voices_.erase(voices_.begin());
        const PcmSound& sound=sounds_.sounds[sound_id];
        const float stored_pan=(static_cast<float>(sound.pan)-64.0F)/63.0F;
        const float mixed_pan=std::clamp(stored_pan+pan,-1.0F,1.0F);
        const float volume=gain*static_cast<float>(sound.volume)/127.0F;
        voices_.push_back(Voice{sound_id,0U,
                               volume*std::min(1.0F,1.0F-mixed_pan),
                               volume*std::min(1.0F,1.0F+mixed_pan)});
        SDL_UnlockAudioDevice(audio_device_);
    }

    void open_controller(int device_index) {
        if (!SDL_IsGameController(device_index)) return;
        const SDL_JoystickID instance=SDL_JoystickGetDeviceInstanceID(device_index);
        if (instance<0) return;
        const auto already_open=std::find_if(controllers_.begin(),controllers_.end(),
                                             [instance](SDL_GameController* controller) {
            if (controller==nullptr) return false;
            return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))==instance;
        });
        if (already_open!=controllers_.end()) return;
        const auto free_port=std::find(controllers_.begin(),controllers_.end(),nullptr);
        if (free_port==controllers_.end()) return;
        SDL_GameController* controller=SDL_GameControllerOpen(device_index);
        if (controller!=nullptr) *free_port=controller;
    }

    void close_controller(SDL_JoystickID instance) {
        for (auto*& controller : controllers_) {
            if (controller==nullptr) continue;
            SDL_Joystick* joystick=SDL_GameControllerGetJoystick(controller);
            if (SDL_JoystickInstanceID(joystick)==instance) {
                SDL_GameControllerClose(controller);
                controller=nullptr;
                return;
            }
        }
    }

    void close_detached_controllers() {
        for (auto*& controller:controllers_) {
            if (controller!=nullptr && !SDL_GameControllerGetAttached(controller)) {
                SDL_GameControllerClose(controller);
                controller=nullptr;
            }
        }
    }

    static ControllerPad read_controller(SDL_GameController* controller) {
        HostControllerState state{};
        state.left_x=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTX);
        state.left_y=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTY);
        state.right_x=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_RIGHTX);
        state.right_y=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_RIGHTY);
        state.left_trigger=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        state.right_trigger=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        const auto copy_button=[&](HostButton destination,SDL_GameControllerButton source) {
            state.buttons[static_cast<std::size_t>(destination)]=
                SDL_GameControllerGetButton(controller,source)!=0;
        };
        copy_button(HostButton::face_south,SDL_CONTROLLER_BUTTON_A);
        copy_button(HostButton::face_west,SDL_CONTROLLER_BUTTON_X);
        copy_button(HostButton::face_east,SDL_CONTROLLER_BUTTON_B);
        copy_button(HostButton::start,SDL_CONTROLLER_BUTTON_START);
        copy_button(HostButton::dpad_up,SDL_CONTROLLER_BUTTON_DPAD_UP);
        copy_button(HostButton::dpad_down,SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        copy_button(HostButton::dpad_left,SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        copy_button(HostButton::dpad_right,SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        copy_button(HostButton::left_shoulder,SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        copy_button(HostButton::right_shoulder,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        return map_host_controller(state);
    }

    struct Voice {
        std::uint16_t sound{};
        std::size_t cursor{};
        float left{},right{};
    };

    static void audio_callback(void* userdata,Uint8* stream,int length) noexcept {
        static_cast<SdlPlatform*>(userdata)->mix_audio(
            reinterpret_cast<std::int16_t*>(stream),length/static_cast<int>(sizeof(std::int16_t)));
    }

    void mix_audio(std::int16_t* output,int sample_count) noexcept {
        std::fill_n(output,sample_count,std::int16_t{0});
        const int frames=sample_count/2;
        for (int frame=0; frame<frames; ++frame) {
            std::int32_t left=0;
            std::int32_t right=0;
            if (!music_.stereo_samples.empty()) {
                left=static_cast<std::int32_t>(
                    music_.stereo_samples[music_cursor_]*music_gain_);
                right=static_cast<std::int32_t>(
                    music_.stereo_samples[music_cursor_+1U]*music_gain_);
                music_cursor_=(music_cursor_+2U)%music_.stereo_samples.size();
            }
            for (Voice& voice:voices_) {
                if (voice.sound>=sounds_.sounds.size()) continue;
                const PcmSound& sound=sounds_.sounds[voice.sound];
                if (voice.cursor>=sound.samples.size()) continue;
                const float sample=static_cast<float>(sound.samples[voice.cursor++]);
                left+=static_cast<std::int32_t>(sample*voice.left);
                right+=static_cast<std::int32_t>(sample*voice.right);
            }
            output[frame*2]=static_cast<std::int16_t>(std::clamp(
                left,static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
            output[frame*2+1]=static_cast<std::int16_t>(std::clamp(
                right,static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
        }
        voices_.erase(std::remove_if(voices_.begin(),voices_.end(),[this](const Voice& voice) {
            return voice.sound>=sounds_.sounds.size() ||
                   voice.cursor>=sounds_.sounds[voice.sound].samples.size();
        }),voices_.end());
    }

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
    SDL_AudioDeviceID audio_device_{};
    SDL_AudioSpec audio_spec_{};
    SoundBank sounds_;
    MusicTrack music_;
    std::size_t music_cursor_{};
    float music_gain_{};
    std::vector<Voice> voices_;
    std::array<SDL_GameController*,4> controllers_{};
};
} // namespace

std::unique_ptr<Platform> Platform::create(std::string_view title) {
    return std::make_unique<SdlPlatform>(title);
}

std::filesystem::path Platform::controller_pak_path() {
    char* base=SDL_GetPrefPath("TheGh0stShip","BattleTanx");
    if (base==nullptr) throw sdl_error("user-data path lookup failed");
    const std::filesystem::path result=std::filesystem::path(base)/"battletanx-pak1.mpk";
    SDL_free(base);
    return result;
}

} // namespace battletanx
