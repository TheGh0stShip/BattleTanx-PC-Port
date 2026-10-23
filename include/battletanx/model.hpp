#pragma once

#include "battletanx/level.hpp"
#include "battletanx/material.hpp"
#include "battletanx/rom.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <array>
#include <vector>

namespace battletanx {

inline constexpr std::size_t material_pool_rom_start=0x5A1110U;
inline constexpr std::size_t material_pool_rom_end=0x6F9C40U;
inline constexpr std::size_t geometry_pool_rom_start=0x3EA970U;
inline constexpr std::size_t geometry_pool_rom_end=0x49C740U;

struct BlobRef {
    std::uint32_t pool_offset{};
    std::uint32_t size{};
};

class BlobView {
  public:
    BlobView(std::span<const std::uint8_t> pool,BlobRef reference);
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::span<const std::uint8_t> at(
        std::uint32_t offset,std::uint32_t length) const;

  private:
    std::span<const std::uint8_t> bytes_;
};

struct MeshVertex {
    float x{},y{},z{};
    float u{},v{};
    std::uint8_t r{},g{},b{},a{};
};

struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

Mesh decode_geometry_blob(std::span<const std::uint8_t> blob,float color_scale=1.0F);

struct ModelGeometryAudit {
    std::size_t part_references{};
    std::size_t material_blobs{};
    std::size_t geometry_blobs{};
    std::size_t vertices{};
    std::size_t triangles{};
};

ModelGeometryAudit audit_model_geometry(const Rom& rom,const WorldLevel& world);

struct NativeModelPart {
    std::uint32_t mesh{};
    std::uint32_t material{};
};

struct NativeModelSet {
    std::uint8_t lod_count{};
    std::array<std::vector<NativeModelPart>,3> lods;
};

class ModelAssets {
  public:
    static ModelAssets load(const Rom& rom,const WorldLevel& world);
    [[nodiscard]] const NativeModelSet& set(std::size_t index) const;
    [[nodiscard]] std::span<const NativeModelSet> sets() const noexcept { return sets_; }
    [[nodiscard]] std::span<const Mesh> meshes() const noexcept { return meshes_; }
    [[nodiscard]] std::span<const NativeMaterial> materials() const noexcept {
        return materials_;
    }
    [[nodiscard]] std::uint32_t set_base() const noexcept { return set_base_; }

  private:
    std::uint32_t set_base_{};
    std::vector<NativeModelSet> sets_;
    std::vector<Mesh> meshes_;
    std::vector<NativeMaterial> materials_;
};

} // namespace battletanx
