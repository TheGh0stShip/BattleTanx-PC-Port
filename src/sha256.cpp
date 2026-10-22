#include "battletanx/sha256.hpp"

#include <iomanip>
#include <sstream>
#include <vector>

namespace battletanx {
namespace {
constexpr std::array<std::uint32_t, 64> kConstants = {
    0x428A2F98,0x71374491,0xB5C0FBCF,0xE9B5DBA5,0x3956C25B,0x59F111F1,0x923F82A4,0xAB1C5ED5,
    0xD807AA98,0x12835B01,0x243185BE,0x550C7DC3,0x72BE5D74,0x80DEB1FE,0x9BDC06A7,0xC19BF174,
    0xE49B69C1,0xEFBE4786,0x0FC19DC6,0x240CA1CC,0x2DE92C6F,0x4A7484AA,0x5CB0A9DC,0x76F988DA,
    0x983E5152,0xA831C66D,0xB00327C8,0xBF597FC7,0xC6E00BF3,0xD5A79147,0x06CA6351,0x14292967,
    0x27B70A85,0x2E1B2138,0x4D2C6DFC,0x53380D13,0x650A7354,0x766A0ABB,0x81C2C92E,0x92722C85,
    0xA2BFE8A1,0xA81A664B,0xC24B8B70,0xC76C51A3,0xD192E819,0xD6990624,0xF40E3585,0x106AA070,
    0x19A4C116,0x1E376C08,0x2748774C,0x34B0BCB5,0x391C0CB3,0x4ED8AA4A,0x5B9CCA4F,0x682E6FF3,
    0x748F82EE,0x78A5636F,0x84C87814,0x8CC70208,0x90BEFFFA,0xA4506CEB,0xBEF9A3F7,0xC67178F2};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}
} // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> data(input.begin(), input.end());
    const std::uint64_t bit_count = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80U);
    while (data.size() % 64U != 56U) data.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>(bit_count >> shift));
    }

    std::array<std::uint32_t, 8> hash = {
        0x6A09E667,0xBB67AE85,0x3C6EF372,0xA54FF53A,
        0x510E527F,0x9B05688C,0x1F83D9AB,0x5BE0CD19};
    for (std::size_t offset = 0; offset < data.size(); offset += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto position = offset + index * 4U;
            words[index] = (static_cast<std::uint32_t>(data[position]) << 24U) |
                           (static_cast<std::uint32_t>(data[position + 1]) << 16U) |
                           (static_cast<std::uint32_t>(data[position + 2]) << 8U) |
                           static_cast<std::uint32_t>(data[position + 3]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const auto first = rotate_right(words[index - 15U], 7U) ^
                               rotate_right(words[index - 15U], 18U) ^
                               (words[index - 15U] >> 3U);
            const auto second = rotate_right(words[index - 2U], 17U) ^
                                rotate_right(words[index - 2U], 19U) ^
                                (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + first + words[index - 7U] + second;
        }
        auto a=hash[0], b=hash[1], c=hash[2], d=hash[3];
        auto e=hash[4], f=hash[5], g=hash[6], h=hash[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sigma1 = rotate_right(e,6U)^rotate_right(e,11U)^rotate_right(e,25U);
            const auto choose = (e&f)^((~e)&g);
            const auto temp1 = h+sigma1+choose+kConstants[index]+words[index];
            const auto sigma0 = rotate_right(a,2U)^rotate_right(a,13U)^rotate_right(a,22U);
            const auto majority = (a&b)^(a&c)^(b&c);
            const auto temp2 = sigma0+majority;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d;
        hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
    }

    std::array<std::uint8_t, 32> result{};
    for (std::size_t index = 0; index < hash.size(); ++index) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            result[index * 4U + byte] =
                static_cast<std::uint8_t>(hash[index] >> (24U - byte * 8U));
        }
    }
    return result;
}

std::string hex_digest(const std::array<std::uint8_t, 32>& digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : digest) output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

} // namespace battletanx

