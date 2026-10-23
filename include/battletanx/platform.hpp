#pragma once

#include "battletanx/gameplay.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace battletanx {

constexpr int native_width=640;
constexpr int native_height=480;

class Platform {
  public:
    virtual ~Platform()=default;
    virtual bool pump(std::array<ControllerPad,4>& controllers)=0;
    virtual void present(std::span<const std::uint32_t> pixels,int width,int height)=0;

    static std::unique_ptr<Platform> create(std::string_view title);
};

} // namespace battletanx
