#include "battletanx/controller_pak.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace battletanx {
namespace {
constexpr std::size_t image_size=32U*1024U;
constexpr std::size_t page_size=256U;
constexpr std::size_t inode_page=1U;
constexpr std::size_t inode_backup_page=2U;
constexpr std::size_t directory_page=3U;
constexpr std::size_t first_data_page=5U;
constexpr std::size_t page_count=128U;
constexpr std::uint16_t inode_eof=1U;
constexpr std::uint16_t inode_free=3U;
constexpr std::uint16_t company_code=0x3548U;
constexpr std::uint32_t game_code=0x4E425845U;
constexpr std::array<std::uint8_t,16> game_name{
    0x1BU,0x1AU,0x2DU,0x2DU,0x25U,0x1EU,0x2DU,0x1AU,
    0x27U,0x31U,0U,0U,0U,0U,0U,0U};

std::uint16_t read16(const std::array<std::uint8_t,image_size>& image,std::size_t offset) {
    return static_cast<std::uint16_t>(image[offset]<<8U|image[offset+1U]);
}

void write16(std::array<std::uint8_t,image_size>& image,std::size_t offset,
             std::uint16_t value) {
    image[offset]=static_cast<std::uint8_t>(value>>8U);
    image[offset+1U]=static_cast<std::uint8_t>(value);
}

std::uint8_t inode_checksum(const std::uint8_t* table) {
    std::uint8_t sum=0U;
    for (std::size_t index=first_data_page*2U; index<page_size; ++index) {
        sum=static_cast<std::uint8_t>(sum+table[index]);
    }
    return sum;
}

bool inode_valid(const std::array<std::uint8_t,image_size>& image,std::size_t page) {
    const auto* table=image.data()+page*page_size;
    return table[1]==inode_checksum(table);
}

void commit_inode(std::array<std::uint8_t,image_size>& image) {
    auto* table=image.data()+inode_page*page_size;
    table[0]=0U;
    table[1]=inode_checksum(table);
    std::copy_n(table,page_size,image.data()+inode_backup_page*page_size);
}

std::array<std::uint8_t,image_size> formatted_image() {
    std::array<std::uint8_t,image_size> image{};
    image[0]=0x81U;
    for (std::size_t index=1U; index<32U; ++index) image[index]=static_cast<std::uint8_t>(index);
    std::array<std::uint8_t,32> id{};
    id[0]=id[1]=id[2]=id[3]=0xFFU;
    id[4]='B'; id[5]='T'; id[6]='N'; id[7]='X';
    id[25]=1U; id[26]=1U;
    std::uint16_t sum=0U;
    for (std::size_t index=0U; index<28U; index+=2U) {
        sum=static_cast<std::uint16_t>(sum+(id[index]<<8U|id[index+1U]));
    }
    const auto inverted=static_cast<std::uint16_t>(0xFFF2U-sum);
    id[28]=static_cast<std::uint8_t>(sum>>8U); id[29]=static_cast<std::uint8_t>(sum);
    id[30]=static_cast<std::uint8_t>(inverted>>8U); id[31]=static_cast<std::uint8_t>(inverted);
    for (const std::size_t block : {1U,3U,4U,6U}) {
        std::copy(id.begin(),id.end(),image.begin()+block*32U);
    }
    for (std::size_t page=0U; page<page_count; ++page) {
        write16(image,inode_page*page_size+page*2U,page<first_data_page ? 0U : inode_free);
    }
    commit_inode(image);
    return image;
}

std::array<std::uint8_t,image_size> read_image(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("failed to open Controller Pak image");
    std::array<std::uint8_t,image_size> image{};
    stream.read(reinterpret_cast<char*>(image.data()),static_cast<std::streamsize>(image.size()));
    if (stream.gcount()!=static_cast<std::streamsize>(image.size()) ||
        stream.peek()!=std::ifstream::traits_type::eof()) {
        throw std::runtime_error("Controller Pak image is not exactly 32 KiB");
    }
    if (!inode_valid(image,inode_page)) {
        if (!inode_valid(image,inode_backup_page)) {
            throw std::runtime_error("Controller Pak inode tables are invalid");
        }
        std::copy_n(image.begin()+inode_backup_page*page_size,page_size,
                    image.begin()+inode_page*page_size);
    }
    return image;
}

bool matching_entry(const std::array<std::uint8_t,image_size>& image,std::size_t slot) {
    const std::size_t offset=directory_page*page_size+slot*32U;
    const std::uint32_t code=static_cast<std::uint32_t>(read16(image,offset))<<16U |
                             read16(image,offset+2U);
    return code==game_code && read16(image,offset+4U)==company_code &&
           std::equal(game_name.begin(),game_name.end(),image.begin()+offset+16U);
}

std::optional<std::size_t> find_entry(const std::array<std::uint8_t,image_size>& image) {
    for (std::size_t slot=0U; slot<16U; ++slot) if (matching_entry(image,slot)) return slot;
    return std::nullopt;
}

void write_image(const std::filesystem::path& path,
                 const std::array<std::uint8_t,image_size>& image) {
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to create Controller Pak image");
    stream.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));
    stream.close();
    if (!stream) throw std::runtime_error("failed to write Controller Pak image");
}

std::uint32_t stored_checksum(const ControllerPakRecord& record) noexcept {
    return static_cast<std::uint32_t>(record.bytes[0])<<24U |
           static_cast<std::uint32_t>(record.bytes[1])<<16U |
           static_cast<std::uint32_t>(record.bytes[2])<<8U |
           static_cast<std::uint32_t>(record.bytes[3]);
}
} // namespace

std::uint32_t controller_pak_checksum(const ControllerPakRecord& record) noexcept {
    std::uint32_t sum=0U;
    for (std::size_t index=4U; index<record.bytes.size(); ++index) sum+=record.bytes[index];
    return sum;
}

bool controller_pak_checksum_valid(const ControllerPakRecord& record) noexcept {
    return stored_checksum(record)==controller_pak_checksum(record);
}

std::optional<ControllerPakRecord> load_controller_pak_record(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return std::nullopt;
    const auto image=read_image(path);
    const auto slot=find_entry(image);
    if (!slot) return std::nullopt;
    const std::size_t directory_offset=directory_page*page_size+*slot*32U;
    const std::uint16_t page=read16(image,directory_offset+6U);
    if (page<first_data_page || page>=page_count ||
        read16(image,inode_page*page_size+page*2U)!=inode_eof) {
        throw std::runtime_error("BattleTanx Controller Pak file chain is invalid");
    }
    ControllerPakRecord record;
    std::copy_n(image.begin()+static_cast<std::size_t>(page)*page_size,page_size,record.bytes.begin());
    if (!controller_pak_checksum_valid(record)) {
        throw std::runtime_error("BattleTanx Controller Pak checksum is invalid");
    }
    return record;
}

void write_controller_pak_record(const std::filesystem::path& path,ControllerPakRecord record) {
    const std::uint32_t checksum=controller_pak_checksum(record);
    record.bytes[0]=static_cast<std::uint8_t>(checksum>>24U);
    record.bytes[1]=static_cast<std::uint8_t>(checksum>>16U);
    record.bytes[2]=static_cast<std::uint8_t>(checksum>>8U);
    record.bytes[3]=static_cast<std::uint8_t>(checksum);
    auto image=std::filesystem::exists(path) ? read_image(path) : formatted_image();
    auto slot=find_entry(image);
    std::size_t page=0U;
    if (slot) {
        page=read16(image,directory_page*page_size+*slot*32U+6U);
    } else {
        for (std::size_t index=0U; index<16U && !slot; ++index) {
            const std::size_t offset=directory_page*page_size+index*32U;
            if (read16(image,offset)==0U && read16(image,offset+2U)==0U) slot=index;
        }
        for (std::size_t index=first_data_page; index<page_count && page==0U; ++index) {
            if (read16(image,inode_page*page_size+index*2U)==inode_free) page=index;
        }
        if (!slot) throw std::runtime_error("Controller Pak directory is full");
        if (page==0U) throw std::runtime_error("Controller Pak has no free page");
        const std::size_t offset=directory_page*page_size+*slot*32U;
        write16(image,offset,static_cast<std::uint16_t>(game_code>>16U));
        write16(image,offset+2U,static_cast<std::uint16_t>(game_code));
        write16(image,offset+4U,company_code);
        write16(image,offset+6U,static_cast<std::uint16_t>(page));
        image[offset+8U]=2U;
        std::copy(game_name.begin(),game_name.end(),image.begin()+offset+16U);
        write16(image,inode_page*page_size+page*2U,inode_eof);
        commit_inode(image);
    }
    if (page<first_data_page || page>=page_count) {
        throw std::runtime_error("BattleTanx Controller Pak page is invalid");
    }
    std::copy(record.bytes.begin(),record.bytes.end(),image.begin()+page*page_size);
    write_image(path,image);
    const auto verified=load_controller_pak_record(path);
    if (!verified || verified->bytes!=record.bytes) {
        throw std::runtime_error("Controller Pak write verification failed");
    }
}

} // namespace battletanx
