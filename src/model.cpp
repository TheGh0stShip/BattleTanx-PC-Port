#include "battletanx/model.hpp"
#include "battletanx/big_endian.hpp"
#include "battletanx/material.hpp"

#include <array>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace battletanx {

BlobView::BlobView(std::span<const std::uint8_t> pool,BlobRef reference) {
    if (reference.pool_offset>pool.size() ||
        reference.size>pool.size()-reference.pool_offset) {
        throw std::out_of_range("model blob exceeds its ROM pool");
    }
    bytes_=pool.subspan(reference.pool_offset,reference.size);
}

std::span<const std::uint8_t> BlobView::at(
    std::uint32_t offset,std::uint32_t length) const {
    if (offset>bytes_.size() || length>bytes_.size()-offset) {
        throw std::out_of_range("model blob-relative range is invalid");
    }
    return bytes_.subspan(offset,length);
}

Mesh decode_geometry_blob(std::span<const std::uint8_t> blob) {
    constexpr std::size_t command_limit=4096U;
    const BigEndianView view(blob);
    Mesh mesh;
    std::array<std::uint32_t,32> slots{};
    std::vector<std::pair<std::uint32_t,std::uint32_t>> vertex_ranges;
    std::uint32_t loaded=0U;
    for (std::size_t command=0U; command<command_limit; ++command) {
        const std::size_t offset=command*8U;
        const std::uint32_t word0=view.u32(offset);
        const std::uint32_t word1=view.u32(offset+4U);
        switch (word0>>24U) {
        case 0x04U: {
            const std::uint32_t count=(word0>>10U)&0x3FU;
            const std::uint32_t length=(word0&0x3FFU)+1U;
            const std::uint32_t raw_start=(word0>>16U)&0xFFU;
            if (count==0U || count>slots.size() || length!=count*16U ||
                (raw_start&1U)!=0U || raw_start!=0U || (word1&7U)!=0U) {
                throw std::runtime_error("geometry blob contains an invalid vertex command");
            }
            const auto vertices=BlobView(blob,{word1,length}).bytes();
            vertex_ranges.emplace_back(word1,length);
            const BigEndianView vertex_view(vertices);
            for (std::uint32_t index=0U; index<count; ++index) {
                const std::size_t vertex_offset=index*16U;
                if (vertex_view.u16(vertex_offset+6U)!=0U) {
                    throw std::runtime_error("geometry vertex has a nonzero flag");
                }
                slots[index]=static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({
                    static_cast<float>(vertex_view.s16(vertex_offset)),
                    static_cast<float>(vertex_view.s16(vertex_offset+2U)),
                    static_cast<float>(vertex_view.s16(vertex_offset+4U)),
                    static_cast<float>(vertex_view.s16(vertex_offset+8U))/64.0F,
                    static_cast<float>(vertex_view.s16(vertex_offset+10U))/64.0F,
                    vertex_view.u8(vertex_offset+12U),vertex_view.u8(vertex_offset+13U),
                    vertex_view.u8(vertex_offset+14U),vertex_view.u8(vertex_offset+15U)});
            }
            loaded=count;
            break;
        }
        case 0xBFU:
            if ((word1>>24U)!=0U) {
                throw std::runtime_error("geometry triangle has a nonzero flag");
            }
            for (const unsigned shift:{16U,8U,0U}) {
                const std::uint32_t raw=(word1>>shift)&0xFFU;
                if ((raw&1U)!=0U || raw/2U>=loaded) {
                    throw std::runtime_error("geometry triangle has an invalid vertex index");
                }
                mesh.indices.push_back(slots[raw/2U]);
            }
            break;
        case 0xB8U:
            if (word0!=0xB8000000U || word1!=0U) {
                throw std::runtime_error("geometry blob contains an invalid end command");
            }
            for (const auto& [start,length]:vertex_ranges) {
                static_cast<void>(length);
                if (start<offset+8U) {
                    throw std::runtime_error("geometry vertex data overlaps its command list");
                }
            }
            return mesh;
        default:
            throw std::runtime_error("geometry blob contains an unsupported command");
        }
    }
    throw std::runtime_error("geometry blob exceeds the command limit");
}

ModelGeometryAudit audit_model_geometry(const Rom& rom,const WorldLevel& world) {
    const auto rom_bytes=rom.bytes();
    const auto material_pool=rom_bytes.subspan(
        material_pool_rom_start,material_pool_rom_end-material_pool_rom_start);
    const auto geometry_pool=rom_bytes.subspan(
        geometry_pool_rom_start,geometry_pool_rom_end-geometry_pool_rom_start);
    std::map<std::uint32_t,std::uint32_t> material_sizes;
    std::map<std::uint32_t,std::uint32_t> geometry_sizes;
    ModelGeometryAudit audit;
    for (const auto& shape:world.shapes) {
        for (std::size_t lod=0U; lod<shape.lod_count; ++lod) {
            const auto& range=world.model_ranges[shape.first_range+lod];
            for (std::size_t index=0U; index<range.part_count; ++index) {
                const auto& part=world.model_parts[range.first_part+index];
                if (part.empty()) continue;
                ++audit.part_references;
                const auto [material_it,material_added]=material_sizes.emplace(
                    part.material_offset,part.material_size);
                if (!material_added && material_it->second!=part.material_size) {
                    throw std::runtime_error("material blob offset has conflicting sizes");
                }
                const auto [geometry_it,geometry_added]=geometry_sizes.emplace(
                    part.geometry_offset,part.geometry_size);
                if (!geometry_added && geometry_it->second!=part.geometry_size) {
                    throw std::runtime_error("geometry blob offset has conflicting sizes");
                }
                static_cast<void>(BlobView(material_pool,
                                           {part.material_offset,part.material_size}));
                if (material_added) {
                    const auto material=BlobView(
                        material_pool,{part.material_offset,part.material_size}).bytes();
                    static_cast<void>(decode_material_blob(material,part.animation));
                }
                if (!geometry_added) continue;
                const auto geometry=BlobView(
                    geometry_pool,{part.geometry_offset,part.geometry_size}).bytes();
                const auto mesh=decode_geometry_blob(geometry);
                audit.vertices+=mesh.vertices.size();
                audit.triangles+=mesh.indices.size()/3U;
            }
        }
    }
    audit.material_blobs=material_sizes.size();
    audit.geometry_blobs=geometry_sizes.size();
    return audit;
}

ModelAssets ModelAssets::load(const Rom& rom,const WorldLevel& world) {
    ModelAssets assets;
    assets.set_base_=world.set_base;
    assets.sets_.resize(world.shapes.size());
    const auto material_pool=rom.bytes().subspan(
        material_pool_rom_start,material_pool_rom_end-material_pool_rom_start);
    const auto geometry_pool=rom.bytes().subspan(
        geometry_pool_rom_start,geometry_pool_rom_end-geometry_pool_rom_start);
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint32_t> mesh_cache;
    std::map<std::tuple<std::uint32_t,std::uint32_t,std::uint16_t>,std::uint32_t>
        material_cache;
    for (std::size_t set_index=0U; set_index<world.shapes.size(); ++set_index) {
        const auto& shape=world.shapes[set_index];
        auto& destination=assets.sets_[set_index];
        destination.lod_count=shape.lod_count;
        for (std::size_t lod=0U; lod<shape.lod_count; ++lod) {
            const auto& range=world.model_ranges[shape.first_range+lod];
            auto& parts=destination.lods[lod];
            parts.reserve(range.part_count);
            for (std::size_t part_index=0U; part_index<range.part_count; ++part_index) {
                const auto& source=world.model_parts[range.first_part+part_index];
                if (source.empty()) continue;
                const auto mesh_key=std::pair{source.geometry_offset,source.geometry_size};
                auto mesh_entry=mesh_cache.find(mesh_key);
                if (mesh_entry==mesh_cache.end()) {
                    const auto blob=BlobView(
                        geometry_pool,{source.geometry_offset,source.geometry_size}).bytes();
                    const auto handle=static_cast<std::uint32_t>(assets.meshes_.size());
                    assets.meshes_.push_back(decode_geometry_blob(blob));
                    mesh_entry=mesh_cache.emplace(mesh_key,handle).first;
                }
                const auto material_key=std::tuple{
                    source.material_offset,static_cast<std::uint32_t>(source.material_size),
                    source.animation};
                auto material_entry=material_cache.find(material_key);
                if (material_entry==material_cache.end()) {
                    const auto blob=BlobView(
                        material_pool,{source.material_offset,source.material_size}).bytes();
                    const auto handle=static_cast<std::uint32_t>(assets.materials_.size());
                    assets.materials_.push_back(
                        decode_material_blob(blob,source.animation));
                    material_entry=material_cache.emplace(material_key,handle).first;
                }
                parts.push_back({mesh_entry->second,material_entry->second});
            }
        }
    }
    return assets;
}

const NativeModelSet& ModelAssets::set(std::size_t index) const {
    if (index>=sets_.size()) throw std::out_of_range("native model set index is invalid");
    return sets_[index];
}

} // namespace battletanx
