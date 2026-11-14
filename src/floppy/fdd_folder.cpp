/*
 * 86Box    Folder-backed FAT12 floppy images.
 *
 *          This file is part of the 86Box distribution.
 */

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

#include <FatLib/FatVolume.h>

#include <86box/disksizes.h>
#include <86box/fdd_folder.h>
#include <86box/media_archive.h>

namespace {

constexpr size_t sector_bytes = 512;

class RamFloppy final : public FsBlockDeviceInterface {
public:
    RamFloppy(uint8_t *data, Sector_t sectors) : data(data), sectors(sectors) {}

    bool isBusy() override { return false; }
    bool syncDevice() override { return true; }
    Sector_t sectorCount() override { return sectors; }

    bool readSector(Sector_t sector, uint8_t *dst) override { return readSectors(sector, dst, 1); }
    bool writeSector(Sector_t sector, const uint8_t *src) override { return writeSectors(sector, src, 1); }

    bool readSectors(Sector_t sector, uint8_t *dst, size_t count) override
    {
        if (sector > sectors || count > sectors - sector)
            return false;
        std::memcpy(dst, data + sector * sector_bytes, count * sector_bytes);
        return true;
    }

    bool writeSectors(Sector_t sector, const uint8_t *src, size_t count) override
    {
        if (sector > sectors || count > sectors - sector)
            return false;
        std::memcpy(data + sector * sector_bytes, src, count * sector_bytes);
        return true;
    }

private:
    uint8_t *data;
    Sector_t sectors;
};

void
write_bpb(uint8_t *data, const disk_size_t &format, uint16_t sectors,
          uint16_t bytes_per_sector, int sector_multiplier)
{
    /* SdFat's formatter supports FAT16 and FAT32. Write a floppy BPB and
       reserve the first two FAT12 entries before handing the volume to SdFat. */
    data[0] = 0xeb;
    data[1] = 0x3c;
    data[2] = 0x90;
    std::memcpy(data + 3, "86BOX5.0", 8);
    setLe16(data + 0x0b, bytes_per_sector);
    data[0x0d] = format.spc * sector_multiplier;
    setLe16(data + 0x0e, sector_multiplier);
    data[0x10] = format.num_fats;
    setLe16(data + 0x11, format.root_dir_entries);
    setLe16(data + 0x13, sectors);
    data[0x15] = format.media_desc;
    setLe16(data + 0x16, format.spfat * sector_multiplier);
    setLe16(data + 0x18, format.sectors * sector_multiplier);
    setLe16(data + 0x1a, format.sides);
    data[0x26] = 0x29;
    std::memcpy(data + 0x2b, "NO NAME    FAT12   ", 19);
    /* INT 18h hands control back to the IBM PC BIOS. The NEC data disk
       uses a self-loop because the same interrupt has a different purpose. */
    const uint8_t pc_boot[] = { 0xcd, 0x18, 0xeb, 0xfe };
    const uint8_t nec_boot[] = { 0xeb, 0xfe };
    if (format.sector_len == disk_sizes[FLOPPY_PC98_1250].sector_len)
        std::memcpy(data + 0x3e, nec_boot, sizeof(nec_boot));
    else
        std::memcpy(data + 0x3e, pc_boot, sizeof(pc_boot));
    data[510] = 0x55;
    data[511] = 0xaa;
}

fdd_folder_result_t
copy_file(const std::filesystem::path &path, FatFile &file,
          std::array<char, 4096> &buffer)
{
    std::ifstream source(path, std::ios::binary);
    if (!source)
        return FDD_FOLDER_READ_ERROR;

    while (source.read(buffer.data(), buffer.size()) || source.gcount()) {
        const auto count = static_cast<size_t>(source.gcount());
        if (file.write(buffer.data(), count) != count)
            return FDD_FOLDER_FULL;
    }
    return source.eof() && !source.bad() ? FDD_FOLDER_OK : FDD_FOLDER_READ_ERROR;
}

bool
valid_name(std::string_view name)
{
    /* SdFat treats these as path or name syntax, so reject lossy names. */
    return name.front() != ' ' && name.back() != ' ' && name.back() != '.' &&
           name.find_first_of("\\:*?\"<>|") == std::string_view::npos;
}

fdd_folder_result_t
copy_directory(const std::filesystem::path &path, FatFile &directory,
               std::array<char, 4096> &buffer)
{
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        const auto status = entry.symlink_status();
        if (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status))
            continue;

        const auto name = entry.path().filename().u8string();
        if (!valid_name(name))
            return FDD_FOLDER_INVALID_NAME;

        FatFile file;
        fdd_folder_result_t result;
        if (std::filesystem::is_directory(status)) {
            if (!file.mkdir(&directory, name.c_str(), false))
                return FDD_FOLDER_CANNOT_CREATE;
            result = copy_directory(entry.path(), file, buffer);
        } else {
            if (!file.open(&directory, name.c_str(), O_WRONLY | O_CREAT | O_EXCL))
                return FDD_FOLDER_CANNOT_CREATE;
            result = copy_file(entry.path(), file, buffer);
        }
        if (!file.close() && result == FDD_FOLDER_OK)
            result = FDD_FOLDER_FULL;
        if (result != FDD_FOLDER_OK)
            return result;
    }
    return FDD_FOLDER_OK;
}

fdd_folder_result_t
copy_archive(void *archive, size_t parent, FatFile &directory, uint64_t &remaining,
             std::array<uint8_t, 4096> &buffer, unsigned depth)
{
    /* Bound stack usage for archive paths with pathological nesting. */
    if (depth > 128)
        return FDD_FOLDER_INVALID_NAME;

    for (size_t id = media_archive_first(archive, parent); id;) {
        const char *name;
        uint64_t size;
        int is_directory;
        const size_t next = media_archive_entry(archive, id, &name, &size, &is_directory);
        if (!valid_name(name))
            return FDD_FOLDER_INVALID_NAME;
        if (size > remaining)
            return FDD_FOLDER_FULL;
        remaining -= size;

        FatFile file;
        fdd_folder_result_t result = FDD_FOLDER_OK;
        if (is_directory) {
            if (!file.mkdir(&directory, name, false))
                return FDD_FOLDER_CANNOT_CREATE;
            result = copy_archive(archive, id, file, remaining, buffer, depth + 1);
        } else {
            if (!file.open(&directory, name, O_WRONLY | O_CREAT | O_EXCL))
                return FDD_FOLDER_CANNOT_CREATE;
            for (uint64_t offset = 0; offset < size;) {
                const auto count = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - offset));
                if (media_archive_read(archive, id, offset, buffer.data(), count) != 1) {
                    result = FDD_FOLDER_READ_ERROR;
                    break;
                }
                if (file.write(buffer.data(), count) != count) {
                    result = FDD_FOLDER_FULL;
                    break;
                }
                offset += count;
            }
        }
        if (!file.close() && result == FDD_FOLDER_OK)
            result = FDD_FOLDER_FULL;
        if (result != FDD_FOLDER_OK)
            return result;
        id = next;
    }
    return FDD_FOLDER_OK;
}

} // namespace

extern "C" fdd_folder_result_t
fdd_folder_create(const char *path, const disk_size_t *format, uint8_t **image)
{
    const auto sectors = static_cast<uint16_t>(format->tracks * format->sides * format->sectors);
    const int multiplier = 1 << (format->sector_len - 2);
    std::unique_ptr<uint8_t, decltype(&std::free)> data(
        static_cast<uint8_t *>(std::calloc(sectors * multiplier, sector_bytes)), &std::free);
    if (!data)
        return FDD_FOLDER_NO_MEMORY;

    /* SdFat uses 512-byte blocks. For PC-98 media, double the layout's sector
       counts during import so the FAT, root and cluster offsets stay the same. */
    if (path)
        write_bpb(data.get(), *format, sectors * multiplier, sector_bytes, multiplier);
    else
        write_bpb(data.get(), *format, sectors, sector_bytes * multiplier, 1);
    for (int i = 0; i < format->num_fats; i++) {
        uint8_t *fat = data.get() + (1 + i * format->spfat) * sector_bytes * multiplier;
        fat[0] = format->media_desc;
        fat[1] = 0xff;
        fat[2] = 0xff;
    }
    if (!path) {
        *image = data.release();
        return FDD_FOLDER_OK;
    }

    RamFloppy device(data.get(), sectors * multiplier);
    FatVolume volume;
    FatFile root;
    // Initialize the superfloppy directly so SdFat does not set a global working volume.
    if (!volume.init(&device, 0) || !root.openRoot(&volume))
        return FDD_FOLDER_CANNOT_CREATE;

    try {
        const auto source = std::filesystem::u8path(path);
        fdd_folder_result_t result;
        if (media_archive_path(path) && !std::filesystem::is_directory(source)) {
            std::unique_ptr<void, decltype(&media_archive_close)> archive(media_archive_open(path), &media_archive_close);
            if (!archive)
                return FDD_FOLDER_READ_ERROR;
            std::array<uint8_t, 4096> buffer;
            uint64_t remaining = device.sectorCount() * sector_bytes;
            result = copy_archive(archive.get(), 0, root, remaining, buffer, 0);
        } else {
            std::array<char, 4096> buffer;
            result = copy_directory(source, root, buffer);
        }
        if (result != FDD_FOLDER_OK)
            return result;
    } catch (const std::bad_alloc &) {
        return FDD_FOLDER_NO_MEMORY;
    } catch (const std::exception &) {
        return FDD_FOLDER_READ_ERROR;
    }
    if (!root.close())
        return FDD_FOLDER_CANNOT_CREATE;

    if (multiplier != 1)
        write_bpb(data.get(), *format, sectors, sector_bytes * multiplier, 1);
    *image = data.release();
    return FDD_FOLDER_OK;
}
