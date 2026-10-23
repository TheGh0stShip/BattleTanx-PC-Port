#include "battletanx/level.hpp"

#include "battletanx/big_endian.hpp"
#include "battletanx/lzari.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace battletanx {
namespace {
constexpr std::size_t kLevelCapacity=0x25800U;
constexpr std::size_t kWorldHeaderSize=0x2E0U;

constexpr std::array<LevelStreamDescriptor,22> kLevels{{
    {0,{0x738900U,0x73B2EDU},std::nullopt},
    {1,{0x73B2F0U,0x742D4EU},RomRange{0x6F9C40U,0x6FEE7BU}},
    {2,{0x742D50U,0x7472BFU},RomRange{0x6FEE80U,0x7017CFU}},
    {3,{0x7472C0U,0x74F0F3U},RomRange{0x7017D0U,0x70510BU}},
    {4,{0x74F0F8U,0x754EEEU},RomRange{0x705110U,0x709533U}},
    {5,{0x754EF0U,0x756FA0U},RomRange{0x709538U,0x709BFEU}},
    {6,{0x756FA0U,0x75EC8FU},RomRange{0x709C00U,0x70D997U}},
    {7,{0x75EC90U,0x767ED4U},RomRange{0x70D998U,0x711DCFU}},
    {8,{0x767ED8U,0x76FE3EU},RomRange{0x711DD0U,0x716B2EU}},
    {9,{0x76FE40U,0x771EA8U},RomRange{0x716B30U,0x7175C6U}},
    {10,{0x771EA8U,0x77A36EU},RomRange{0x7175C8U,0x71B3DAU}},
    {11,{0x77A370U,0x783327U},RomRange{0x71B3E0U,0x722A4DU}},
    {12,{0x783328U,0x78A974U},RomRange{0x722A50U,0x726272U}},
    {13,{0x78A978U,0x7909F2U},RomRange{0x726278U,0x727F76U}},
    {14,{0x7909F8U,0x79731DU},RomRange{0x727F78U,0x72D02CU}},
    {15,{0x797320U,0x79EB33U},RomRange{0x72D030U,0x730C8CU}},
    {16,{0x79EB38U,0x7A6187U},RomRange{0x731460U,0x73789BU}},
    {17,{0x7A6188U,0x7A8868U},RomRange{0x730C90U,0x731460U}},
    {24,{0x7A8868U,0x7ADEE6U},RomRange{0x7378A0U,0x7388F5U}},
    {25,{0x7ADEE8U,0x7B0F3FU},std::nullopt},
    {26,{0x7B0F40U,0x7B4508U},std::nullopt},
    {27,{0x7B4508U,0x7B69A0U},std::nullopt},
}};

std::span<const std::uint8_t> checked_range(const Rom& rom,RomRange range) {
    if (range.start>=range.end || range.end>rom.bytes().size()) {
        throw std::runtime_error("level stream range is outside the verified ROM");
    }
    return rom.bytes().subspan(range.start,range.end-range.start);
}

std::vector<std::uint8_t> decode(const Rom& rom,RomRange range) {
    return lzari_decompress(checked_range(rom,range),kLevelCapacity);
}

void require_section_offsets(std::span<const std::uint8_t> bytes,
                             std::span<const std::size_t> offsets) {
    if (!std::is_sorted(offsets.begin(),offsets.end()) || offsets.back()!=bytes.size()) {
        throw std::runtime_error("level section offsets do not cover their decoded stream");
    }
}

bool cross_is_negative(NavigationVertex a,NavigationVertex origin,
                       NavigationVertex point) {
    const float az=a.z-origin.z;
    const float ax=a.x-origin.x;
    const float px=point.x-origin.x;
    const float pz=point.z-origin.z;
    return az*px-pz*ax<0.0F;
}

const NavigationCorner& navigation_corner(const NavigationLevel& level,
                                          std::uint16_t corner) {
    const std::size_t triangle=corner/3U;
    if (triangle>=level.triangles.size()) {
        throw std::out_of_range("navigation corner index is invalid");
    }
    return level.triangles[triangle].corners[corner%3U];
}
} // namespace

std::span<const LevelStreamDescriptor> level_stream_descriptors() noexcept { return kLevels; }

WorldLevel parse_world_level(std::span<const std::uint8_t> bytes) {
    if (bytes.size()<0x24U+kWorldHeaderSize) {
        throw std::runtime_error("world stream is shorter than its fixed header");
    }
    const BigEndianView view(bytes);
    WorldLevel result;
    for (std::size_t index=0; index<result.section_offsets.size(); ++index) {
        result.section_offsets[index]=view.u32(index*4U);
    }
    if (result.section_offsets.front()!=0x24U) {
        throw std::runtime_error("world stream has an invalid offset header");
    }
    require_section_offsets(bytes,result.section_offsets);
    constexpr std::array<std::size_t,8> record_sizes{
        kWorldHeaderSize,0x14U,0x1CU,0x0CU,0x04U,0x04U,0x10U,0x10U};
    std::array<std::size_t,8> counts{};
    for (std::size_t index=0; index<counts.size(); ++index) {
        const std::size_t size=result.section_offsets[index+1U]-result.section_offsets[index];
        if (size%record_sizes[index]!=0U) {
            throw std::runtime_error("world stream contains a malformed fixed-record section");
        }
        counts[index]=size/record_sizes[index];
    }
    if (counts[0]!=1U || counts[7]!=0U) {
        throw std::runtime_error("world stream has an invalid header or trailing section");
    }
    const std::size_t header=result.section_offsets[0];
    result.flags=view.u32(header+0x20U);
    const std::int32_t declared_groups=view.s32(header+0x27CU);
    const std::int32_t optional_group=view.s32(header+0x280U);
    if (declared_groups<0 || static_cast<std::size_t>(declared_groups)+
        (optional_group!=-1 ? 1U : 0U)!=counts[1]) {
        throw std::runtime_error("world group count disagrees with its section");
    }
    const std::size_t group_start=result.section_offsets[1];
    std::size_t covered_items=0U;
    result.groups.reserve(counts[1]);
    for (std::size_t index=0; index<counts[1]; ++index) {
        const std::size_t offset=group_start+index*0x14U;
        WorldGroup group;
        group.item_count=view.u16(offset);
        group.first_item=view.u16(offset+2U);
        for (std::size_t word=0; word<group.bounds.size(); ++word) {
            group.bounds[word]=view.s32(offset+4U+word*4U);
        }
        const std::size_t end=static_cast<std::size_t>(group.first_item)+group.item_count;
        if (end>counts[2]) throw std::runtime_error("world group item range exceeds its section");
        covered_items=std::max(covered_items,end);
        result.groups.push_back(group);
    }
    if (covered_items!=counts[2]) {
        throw std::runtime_error("world groups do not cover the item section");
    }
    const std::size_t item_start=result.section_offsets[2];
    result.items.reserve(counts[2]);
    for (std::size_t index=0; index<counts[2]; ++index) {
        const std::size_t offset=item_start+index*0x1CU;
        WorldItem item;
        item.flags=view.u8(offset);
        item.kind=view.u8(offset+1U);
        item.parameter=view.u8(offset+2U);
        item.attributes=view.u8(offset+3U);
        item.id=view.u16(offset+4U);
        item.type=view.s32(offset+8U);
        item.x=view.f32(offset+0xCU);
        item.z=view.f32(offset+0x10U);
        item.angle=view.u16(offset+0x14U);
        item.link=view.s16(offset+0x16U);
        item.shape=view.s16(offset+0x18U);
        if ((item.flags&1U)==0U && (item.shape<0 || static_cast<std::size_t>(item.shape)>=counts[3])) {
            throw std::runtime_error("world item shape index exceeds its section");
        }
        result.items.push_back(item);
    }
    const std::size_t shape_start=result.section_offsets[3];
    result.shapes.reserve(counts[3]);
    for (std::size_t index=0; index<counts[3]; ++index) {
        const std::size_t offset=shape_start+index*0x0CU;
        result.shapes.push_back({view.u8(offset),view.u16(offset+2U),
                                 view.s16(offset+4U),view.s16(offset+6U),
                                 view.s16(offset+8U),view.s16(offset+0xAU)});
    }
    return result;
}

NavigationLevel parse_navigation_level(std::span<const std::uint8_t> bytes) {
    if (bytes.size()<0x28U) throw std::runtime_error("navigation stream is shorter than its header");
    const BigEndianView view(bytes);
    if (view.u32(0)!=bytes.size() || view.u32(4)!=bytes.size()) {
        throw std::runtime_error("navigation stream size words disagree with its decoded size");
    }
    const std::size_t vertex_count=view.u16(8U);
    const std::size_t triangle_count=view.u16(0xAU);
    if (triangle_count>std::numeric_limits<std::uint16_t>::max()/3U) {
        throw std::runtime_error("navigation stream has too many flattened corners");
    }
    std::array<std::size_t,5> offsets{};
    for (std::size_t index=0; index<offsets.size(); ++index) {
        offsets[index]=view.u32(0xCU+index*4U);
    }
    if (offsets[0]!=0x28U || offsets[1]!=offsets[2] ||
        !std::is_sorted(offsets.begin(),offsets.end()) || offsets.back()>bytes.size()) {
        throw std::runtime_error("navigation stream has invalid section offsets");
    }
    if (view.u32(0x20U)!=0x02000000U || view.u32(0x24U)!=0U) {
        throw std::runtime_error("navigation stream has an invalid fixed header tail");
    }
    const std::size_t triangle_end=offsets[2]+triangle_count*12U;
    const std::size_t kind_end=offsets[3]+triangle_count;
    if (offsets[0]+vertex_count*8U!=offsets[1] ||
        triangle_end>offsets[3] || kind_end>offsets[4] || bytes.size()-offsets[4]!=256U) {
        throw std::runtime_error("navigation stream section sizes disagree with their counts");
    }
    const auto triangle_padding=view.subspan(triangle_end,offsets[3]-triangle_end);
    const auto kind_padding=view.subspan(kind_end,offsets[4]-kind_end);
    if (std::any_of(triangle_padding.begin(),triangle_padding.end(),
                    [](std::uint8_t value) { return value!=0U; }) ||
        std::any_of(kind_padding.begin(),kind_padding.end(),
                    [](std::uint8_t value) { return value!=0U; })) {
        throw std::runtime_error("navigation stream alignment padding is not zero");
    }
    NavigationLevel result;
    result.vertices.reserve(vertex_count);
    for (std::size_t index=0; index<vertex_count; ++index) {
        const std::size_t offset=offsets[0]+index*8U;
        result.vertices.push_back({view.f32(offset),view.f32(offset+4U)});
    }
    result.triangles.reserve(triangle_count);
    for (std::size_t index=0; index<triangle_count; ++index) {
        NavigationTriangle triangle;
        for (std::size_t corner=0; corner<triangle.corners.size(); ++corner) {
            const std::size_t offset=offsets[2]+(index*3U+corner)*4U;
            triangle.corners[corner]={view.u16(offset),view.u16(offset+2U)};
            if (index!=0U && (triangle.corners[corner].vertex==0U ||
                triangle.corners[corner].vertex>=vertex_count)) {
                throw std::runtime_error("navigation corner vertex index is invalid");
            }
            if (triangle.corners[corner].adjacent!=0U &&
                (triangle.corners[corner].adjacent<3U ||
                 triangle.corners[corner].adjacent>=triangle_count*3U)) {
                throw std::runtime_error("navigation corner adjacency is invalid");
            }
        }
        triangle.kind=view.u8(offsets[3]+index);
        result.triangles.push_back(triangle);
    }
    const auto enabled=view.subspan(offsets[4],result.kind_enabled.size());
    std::copy(enabled.begin(),enabled.end(),result.kind_enabled.begin());
    return result;
}

WorldLevel load_world_level(const Rom& rom,const LevelStreamDescriptor& descriptor) {
    return parse_world_level(decode(rom,descriptor.world));
}

NavigationLevel load_navigation_level(const Rom& rom,const LevelStreamDescriptor& descriptor) {
    if (!descriptor.navigation) {
        throw std::runtime_error("level "+std::to_string(descriptor.index)+
                                 " has no navigation stream");
    }
    return parse_navigation_level(decode(rom,*descriptor.navigation));
}

NavigationVertex navigation_triangle_centroid(const NavigationLevel& level,
                                               std::uint16_t triangle) {
    if (triangle==0U || triangle>=level.triangles.size()) {
        throw std::out_of_range("navigation triangle index is invalid");
    }
    NavigationVertex result{};
    for (const auto& corner : level.triangles[triangle].corners) {
        if (corner.vertex==0U || corner.vertex>=level.vertices.size()) {
            throw std::out_of_range("navigation triangle vertex index is invalid");
        }
        result.x+=level.vertices[corner.vertex].x;
        result.z+=level.vertices[corner.vertex].z;
    }
    result.x/=3.0F;
    result.z/=3.0F;
    return result;
}

NavigationVertex navigation_edge_midpoint(const NavigationLevel& level,std::uint16_t corner) {
    const std::size_t triangle=corner/3U;
    const std::size_t within=corner%3U;
    if (triangle==0U || triangle>=level.triangles.size()) {
        throw std::out_of_range("navigation corner index is invalid");
    }
    const auto& corners=level.triangles[triangle].corners;
    const std::size_t first=corners[within].vertex;
    const std::size_t second=corners[(within+1U)%3U].vertex;
    if (first==0U || second==0U || first>=level.vertices.size() ||
        second>=level.vertices.size()) {
        throw std::out_of_range("navigation edge vertex index is invalid");
    }
    return {(level.vertices[first].x+level.vertices[second].x)/2.0F,
            (level.vertices[first].z+level.vertices[second].z)/2.0F};
}

bool navigation_point_inside_triangle(const NavigationLevel& level,
                                      NavigationVertex point,std::uint16_t triangle) {
    if (triangle==0U || triangle>=level.triangles.size()) return false;
    const auto& corners=level.triangles[triangle].corners;
    for (std::size_t index=0; index<corners.size(); ++index) {
        const std::size_t next=(index+1U)%corners.size();
        if (corners[index].vertex==0U || corners[next].vertex==0U ||
            corners[index].vertex>=level.vertices.size() ||
            corners[next].vertex>=level.vertices.size()) return false;
        const auto& a=level.vertices[corners[index].vertex];
        const auto& origin=level.vertices[corners[next].vertex];
        if (!cross_is_negative(a,origin,point)) return false;
    }
    return true;
}

std::uint16_t find_nearest_navigation_triangle(const NavigationLevel& level,
                                               NavigationVertex point) {
    std::uint16_t nearest_distance=16000U;
    std::uint16_t nearest=0U;
    const std::size_t limit=std::min<std::size_t>(
        level.triangles.size(),static_cast<std::size_t>(
            std::numeric_limits<std::uint16_t>::max())+1U);
    for (std::size_t index=1U; index<limit; ++index) {
        const auto& triangle=level.triangles[index];
        if (level.kind_enabled[triangle.kind]!=0U) continue;
        const auto center=navigation_triangle_centroid(level,static_cast<std::uint16_t>(index));
        const float dx=std::abs(point.x-center.x);
        const float dz=std::abs(point.z-center.z);
        const float major=std::max(dx,dz);
        const float minor=std::min(dx,dz);
        const float distance=major+minor*3.0F/8.0F;
        if (std::isfinite(distance) && distance>=0.0F &&
            distance<static_cast<float>(nearest_distance)) {
            nearest_distance=static_cast<std::uint16_t>(distance);
            nearest=static_cast<std::uint16_t>(index);
        }
    }
    return nearest;
}

NavigationTrace trace_navigation_segment(const NavigationLevel& level,
                                         std::uint16_t start_triangle,
                                         NavigationVertex from,
                                         NavigationVertex to,
                                         std::uint8_t blocked_mask) {
    if (level.triangles.size()>std::numeric_limits<std::uint16_t>::max()/3U) {
        throw std::length_error("navigation level has too many flattened corners");
    }
    if (start_triangle==0U || start_triangle>=level.triangles.size()) {
        throw std::out_of_range("navigation trace start triangle is invalid");
    }
    NavigationTrace result;
    result.triangle=start_triangle;
    if (navigation_point_inside_triangle(level,to,start_triangle)) {
        result.stop=NavigationTraceStop::target;
        return result;
    }

    const auto select_edge=[&](std::uint16_t triangle) {
        const auto& corners=level.triangles[triangle].corners;
        std::array<bool,3> sides{};
        for (std::size_t index=0; index<sides.size(); ++index) {
            const auto vertex=corners[index].vertex;
            if (vertex==0U || vertex>=level.vertices.size()) {
                throw std::out_of_range("navigation trace vertex index is invalid");
            }
            sides[index]=cross_is_negative(from,level.vertices[vertex],to);
        }
        for (std::size_t index=0; index<sides.size(); ++index) {
            if (sides[index] && !sides[(index+1U)%sides.size()]) {
                return static_cast<std::uint16_t>(triangle*3U+index);
            }
        }
        return std::uint16_t{0};
    };

    std::uint16_t edge=select_edge(start_triangle);
    if (edge==0U) return result;
    for (std::size_t attempt=0; attempt<0xF8U; ++attempt) {
        result.edges.push_back(edge);
        const std::size_t triangle=edge/3U;
        const std::size_t side=edge%3U;
        const auto& corners=level.triangles[triangle].corners;
        const auto first=corners[side].vertex;
        const auto second=corners[(side+1U)%3U].vertex;
        if (first==0U || second==0U || first>=level.vertices.size() ||
            second>=level.vertices.size()) {
            throw std::out_of_range("navigation trace edge vertex index is invalid");
        }
        if (cross_is_negative(level.vertices[first],level.vertices[second],to)) {
            result.triangle=static_cast<std::uint16_t>(triangle);
            result.stop=NavigationTraceStop::target;
            return result;
        }
        const std::uint16_t adjacent=navigation_corner(level,edge).adjacent;
        if (adjacent==0U) {
            result.triangle=static_cast<std::uint16_t>(triangle);
            result.stop=NavigationTraceStop::boundary;
            return result;
        }
        const std::uint16_t next_triangle=adjacent/3U;
        if (next_triangle==0U || next_triangle>=level.triangles.size()) {
            throw std::out_of_range("navigation trace adjacency is invalid");
        }
        const auto status=level.kind_enabled[level.triangles[next_triangle].kind];
        if ((status&blocked_mask)!=0U) {
            result.triangle=next_triangle;
            result.stop=NavigationTraceStop::blocked;
            return result;
        }
        const std::uint16_t next_side=static_cast<std::uint16_t>((adjacent+2U)%3U);
        const auto& next_corners=level.triangles[next_triangle].corners;
        const auto vertex=next_corners[next_side].vertex;
        if (vertex==0U || vertex>=level.vertices.size()) {
            throw std::out_of_range("navigation trace vertex index is invalid");
        }
        const std::uint16_t base=static_cast<std::uint16_t>(next_triangle*3U);
        edge=cross_is_negative(from,level.vertices[vertex],to)
            ? static_cast<std::uint16_t>(base+next_side)
            : static_cast<std::uint16_t>(base+(next_side+2U)%3U);
        result.triangle=next_triangle;
    }
    result.stop=NavigationTraceStop::iteration_limit;
    return result;
}

} // namespace battletanx
