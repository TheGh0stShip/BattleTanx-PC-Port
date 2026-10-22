#include "battletanx/bundle.hpp"
#include "battletanx/big_endian.hpp"

#include <stdexcept>

namespace battletanx {
Bundle Bundle::parse(std::span<const std::uint8_t> bytes) {
    const BigEndianView view(bytes);
    if (bytes.size()<8U) throw std::runtime_error("resource bundle is too short");
    const std::size_t count=view.u32(0);
    if (count==0U || count>0x400U) throw std::runtime_error("invalid resource bundle entry count");
    if (count>(bytes.size()-8U)/4U) throw std::runtime_error("resource bundle table is truncated");
    const std::size_t expected_payload=(8U+4U*count+7U)&~std::size_t{7U};
    const std::size_t payload=view.u32(4);
    if (payload!=expected_payload || payload>bytes.size()) {
        throw std::runtime_error("invalid resource bundle payload offset");
    }
    if (view.u32(4U+4U*count)!=0U) {
        throw std::runtime_error("resource bundle table has no zero terminator");
    }
    Bundle bundle;
    bundle.payload_offset_=payload;
    std::size_t previous=0U;
    for (std::size_t index=0; index<count; ++index) {
        const std::size_t offset=view.u32(4U+4U*index);
        const std::size_t end=index+1U<count ? view.u32(8U+4U*index) : bytes.size();
        if (offset<bundle.payload_offset_ || offset>=end || end>bytes.size() ||
            (index>0U && offset<=previous)) {
            throw std::runtime_error("resource bundle offsets are invalid");
        }
        bundle.entries_.push_back({offset,end-offset});
        previous=offset;
    }
    return bundle;
}
} // namespace battletanx

