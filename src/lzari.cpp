#include "battletanx/lzari.hpp"
#include "battletanx/big_endian.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace battletanx {
namespace {
constexpr int kRingSize=4096, kLookahead=60, kThreshold=2, kCharacterCount=314;
constexpr std::uint32_t kQ1=1U<<15U, kQ2=2U*kQ1, kQ3=3U*kQ1, kQ4=4U*kQ1;
constexpr std::uint32_t kMaxCumulative=kQ1-1U;

class Decoder {
  public:
    explicit Decoder(std::span<const std::uint8_t> source) : source_(source) {
        for (int symbol=kCharacterCount; symbol>0; --symbol) {
            symbol_to_character_[symbol]=symbol-1;
            symbol_frequency_[symbol]=1;
            symbol_cumulative_[symbol-1]=symbol_cumulative_[symbol]+1U;
        }
        for (int index=kRingSize; index>0; --index) {
            position_cumulative_[index-1]=position_cumulative_[index]+10000U/
                static_cast<std::uint32_t>(index+200);
        }
    }
    void start() { for (int index=0; index<17; ++index) value_=value_*2U+bit(); }
    int character() {
        const auto range=high_-low_;
        const auto scaled=((value_-low_+1U)*symbol_cumulative_[0]-1U)/range;
        const int symbol=search(symbol_cumulative_,scaled,kCharacterCount);
        high_=low_+range*symbol_cumulative_[symbol-1]/symbol_cumulative_[0];
        low_+=range*symbol_cumulative_[symbol]/symbol_cumulative_[0];
        renormalize();
        const int result=symbol_to_character_[symbol];
        update(symbol);
        return result;
    }
    int position() {
        const auto range=high_-low_;
        const auto scaled=((value_-low_+1U)*position_cumulative_[0]-1U)/range;
        const int result=search(position_cumulative_,scaled,kRingSize)-1;
        high_=low_+range*position_cumulative_[result]/position_cumulative_[0];
        low_+=range*position_cumulative_[result+1]/position_cumulative_[0];
        renormalize();
        return result;
    }
  private:
    int bit() {
        mask_>>=1;
        if (mask_==0) {
            if (offset_>=source_.size()) {
                padding_bits_+=8;
                if (padding_bits_>32) throw std::runtime_error("truncated LZARI stream");
                byte_=0xFF;
            } else byte_=source_[offset_++];
            mask_=0x80;
        }
        return (byte_&mask_)!=0;
    }
    template<std::size_t Size>
    static int search(const std::array<std::uint32_t,Size>& cumulative,
                      std::uint32_t value,int high) {
        int low=1;
        while (low<high) {
            const int middle=(low+high)/2;
            if (cumulative[middle]>value) low=middle+1; else high=middle;
        }
        return low;
    }
    void update(int symbol) {
        if (symbol_cumulative_[0]>=kMaxCumulative) {
            std::uint32_t total=0;
            for (int index=kCharacterCount; index>0; --index) {
                symbol_cumulative_[index]=total;
                symbol_frequency_[index]=(symbol_frequency_[index]+1U)>>1U;
                total+=symbol_frequency_[index];
            }
            symbol_cumulative_[0]=total;
        }
        int index=symbol;
        while (symbol_frequency_[index]==symbol_frequency_[index-1]) --index;
        if (index<symbol) std::swap(symbol_to_character_[index],symbol_to_character_[symbol]);
        ++symbol_frequency_[index];
        --index;
        while (index>=0) ++symbol_cumulative_[index--];
    }
    void renormalize() {
        for (;;) {
            if (low_>=kQ2) { value_-=kQ2; low_-=kQ2; high_-=kQ2; }
            else if (low_>=kQ1 && high_<=kQ3) { value_-=kQ1; low_-=kQ1; high_-=kQ1; }
            else if (high_>kQ2) return;
            low_*=2U; high_*=2U; value_=value_*2U+bit();
        }
    }
    std::span<const std::uint8_t> source_;
    std::size_t offset_{};
    int mask_{},byte_{},padding_bits_{};
    std::uint32_t low_{},high_{kQ4},value_{};
    std::array<int,kCharacterCount+1> symbol_to_character_{};
    std::array<std::uint32_t,kCharacterCount+1> symbol_frequency_{},symbol_cumulative_{};
    std::array<std::uint32_t,kRingSize+1> position_cumulative_{};
};
} // namespace

std::vector<std::uint8_t> lzari_decompress(
    std::span<const std::uint8_t> stream,std::size_t max_output) {
    if (stream.size()<4U) throw std::runtime_error("LZARI stream is too short");
    const std::size_t output_size=BigEndianView(stream).u32(0);
    if (output_size>max_output) throw std::runtime_error("LZARI output exceeds configured limit");
    if (output_size==0U) return {};
    if (stream.size()<7U) throw std::runtime_error("LZARI stream is too short");
    Decoder decoder(stream.subspan(4));
    decoder.start();
    std::array<std::uint8_t,kRingSize> ring{};
    std::fill_n(ring.begin(),kRingSize-kLookahead,std::uint8_t{' '});
    int ring_offset=kRingSize-kLookahead;
    std::vector<std::uint8_t> output;
    output.reserve(output_size);
    while (output.size()<output_size) {
        const int character=decoder.character();
        if (character<256) {
            const auto value=static_cast<std::uint8_t>(character);
            output.push_back(value);
            ring[ring_offset]=value;
            ring_offset=(ring_offset+1)&(kRingSize-1);
            continue;
        }
        const int source_offset=(ring_offset-decoder.position()-1)&(kRingSize-1);
        const int length=character-255+kThreshold;
        if (output.size()+static_cast<std::size_t>(length)>output_size) {
            throw std::runtime_error("LZARI match exceeds declared output size");
        }
        for (int index=0; index<length; ++index) {
            const auto value=ring[(source_offset+index)&(kRingSize-1)];
            output.push_back(value);
            ring[ring_offset]=value;
            ring_offset=(ring_offset+1)&(kRingSize-1);
        }
    }
    return output;
}
} // namespace battletanx

