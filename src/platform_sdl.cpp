#include "battletanx/platform.hpp"
#include "battletanx/host_input.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

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
        for (int index=0; index<SDL_NumJoysticks(); ++index) open_controller(index);
    }

    ~SdlPlatform() override {
        shutdown();
    }

  private:
    void shutdown() noexcept {
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
        SDL_Event event{};
        while (SDL_PollEvent(&event)!=0) {
            if (event.type==SDL_QUIT) return false;
            if (event.type==SDL_CONTROLLERDEVICEADDED) open_controller(event.cdevice.which);
            if (event.type==SDL_CONTROLLERDEVICEREMOVED) close_controller(event.cdevice.which);
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

    void open_controller(int device_index) {
        if (!SDL_IsGameController(device_index)) return;
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

    static ControllerPad read_controller(SDL_GameController* controller) {
        HostControllerState state{};
        state.left_x=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTX);
        state.left_y=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTY);
        state.right_x=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_RIGHTX);
        state.right_y=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_RIGHTY);
        state.left_trigger=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        const auto copy_button=[&](HostButton destination,SDL_GameControllerButton source) {
            state.buttons[static_cast<std::size_t>(destination)]=
                SDL_GameControllerGetButton(controller,source)!=0;
        };
        copy_button(HostButton::face_south,SDL_CONTROLLER_BUTTON_A);
        copy_button(HostButton::face_west,SDL_CONTROLLER_BUTTON_X);
        copy_button(HostButton::start,SDL_CONTROLLER_BUTTON_START);
        copy_button(HostButton::dpad_up,SDL_CONTROLLER_BUTTON_DPAD_UP);
        copy_button(HostButton::dpad_down,SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        copy_button(HostButton::dpad_left,SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        copy_button(HostButton::dpad_right,SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        copy_button(HostButton::left_shoulder,SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        copy_button(HostButton::right_shoulder,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        return map_host_controller(state);
    }

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
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
