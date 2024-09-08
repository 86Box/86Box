/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Media history management module
 *
 *
 *
 * Authors: cold-brewed
 *
 *          Copyright 2022 The 86Box development team
 */

#include <QApplication>
#include <QFileInfo>
#include <QMetaEnum>
#include <QStringBuilder>
#include <utility>
#include "qt_mediahistorymanager.hpp"

extern "C" {
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/fdd.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/path.h>
}

namespace ui {

MediaHistoryManager::MediaHistoryManager()
{
    initializeImageHistory();
    deserializeAllImageHistory();
    initialDeduplication();
}

MediaHistoryManager::~MediaHistoryManager()
    = default;

master_list_t &
MediaHistoryManager::blankImageHistory(master_list_t &initialized_master_list) const
{
    for (const auto device_type : ui::AllSupportedMediaHistoryTypes) {
        device_media_history_t device_media_history;
        // Loop for all possible media devices
        for (int device_index = 0; device_index < maxDevicesSupported(device_type); device_index++) {
            device_index_list_t indexing_list;
            device_media_history[device_index] = indexing_list;
            // Loop for each history slot
            for (int slot_index = 0; slot_index < max_images; slot_index++) {
                device_media_history[device_index].append(QString());
            }
        }
        initialized_master_list.insert(device_type, device_media_history);
    }
    return initialized_master_list;
}

const device_index_list_t &
MediaHistoryManager::getHistoryListForDeviceIndex(int index, ui::MediaType type)
{
    if (master_list.contains(type)) {
        if ((index >= 0) && (index < master_list[type].size())) {
            return master_list[type][index];
        } else {
            qWarning("Media device index %i for device type %s was requested but index %i is out of range (valid range: >= 0 && < %lli)",
                     index, mediaTypeToString(type).toUtf8().constData(), index, static_cast<long long>(master_list[type].size()));
        }
    }
    // Failure gets an empty list
    return empty_device_index_list;
}

void
MediaHistoryManager::setHistoryListForDeviceIndex(int index, ui::MediaType type, device_index_list_t history_list)
{
    master_list[type][index] = std::move(history_list);
}

QString
MediaHistoryManager::getImageForSlot(int index, int slot, ui::MediaType type)
{
    QString             image_name;
    device_index_list_t device_history = getHistoryListForDeviceIndex(index, type);
    if ((slot >= 0) && (slot < device_history.size())) {
        image_name = device_history[slot];
    } else {
        qWarning("Media history slot %i, index %i for device type %s was requested but slot %i is out of range (valid range: >= 0 && < %i, device_history.size() is %lli)",
                 slot, index, mediaTypeToString(type).toUtf8().constData(), slot, maxDevicesSupported(type), static_cast<long long>(device_history.size()));
    }
    return image_name;
}

// These are hardcoded since we can't include the various
// header files where they are defined (e.g., fdd.h, mo.h).
// However, all in ui::MediaType support 4 except cassette.
int
MediaHistoryManager::maxDevicesSupported(ui::MediaType type)
{
    switch (type) {
        default:
            return 4;
        case ui::MediaType::Cassette:
            return 1;
        case ui::MediaType::Cartridge:
            return 2;
    }
}

void
MediaHistoryManager::deserializeImageHistoryType(ui::MediaType type)
{
    for (int device = 0; device < maxDevicesSupported(type); device++) {
        char **device_history_ptr = getEmuHistoryVarForType(type, device);
        if (device_history_ptr == nullptr) {
            // Device not supported, return and do not deserialize.
            // This will leave the image listing at the default initialization state
            // from the ui side (this class)
            continue;
        }
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            master_list[type][device][slot] = device_history_ptr[slot];
        }
    }
}
void
MediaHistoryManager::deserializeAllImageHistory()
{
    for (const auto device_type : ui::AllSupportedMediaHistoryTypes) {
        deserializeImageHistoryType(device_type);
    }
}
void
MediaHistoryManager::serializeImageHistoryType(ui::MediaType type)
{
    for (int device = 0; device < maxDevicesSupported(type); device++) {
        char **device_history_ptr = getEmuHistoryVarForType(type, device);
        if (device_history_ptr == nullptr) {
            // Device not supported, return and do not serialize.
            // This will leave the image listing at the current state,
            // and it will not be saved on the emu side
            continue;
        }
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            if (device_history_ptr[slot] != nullptr) {
                strncpy(device_history_ptr[slot], master_list[type][device][slot].toUtf8().constData(), MAX_IMAGE_PATH_LEN);
            }
        }
    }
}

void
MediaHistoryManager::serializeAllImageHistory()
{
    for (const auto device_type : ui::AllSupportedMediaHistoryTypes) {
        serializeImageHistoryType(device_type);
    }
}

void
MediaHistoryManager::initialDeduplication()
{

    QString current_image;
    // Perform initial dedup if an image is loaded
    for (const auto device_type : ui::AllSupportedMediaHistoryTypes) {
        for (int device_index = 0; device_index < maxDevicesSupported(device_type); device_index++) {
            device_index_list_t device_history = getHistoryListForDeviceIndex(device_index, device_type);
            switch (device_type) {
                default:
                    continue;
                    break;
                case ui::MediaType::Cassette:
                    current_image = cassette_fname;
                    break;
                case ui::MediaType::Cartridge:
                    current_image = cart_fns[device_index];
                    break;
                case ui::MediaType::Floppy:
                    current_image = floppyfns[device_index];
                    break;
                case ui::MediaType::Optical:
                    current_image = cdrom[device_index].image_path;
                    break;
                case ui::MediaType::Zip:
                    current_image = zip_drives[device_index].image_path;
                    break;
                case ui::MediaType::Mo:
                    current_image = mo_drives[device_index].image_path;
                    break;
            }
            deduplicateList(device_history, QVector<QString>(1, current_image));
            // Fill in missing, if any
            int missing = MAX_PREV_IMAGES - device_history.size();
            if (missing) {
                for (int i = 0; i < missing; i++) {
                    device_history.push_back(QString());
                }
            }
            setHistoryListForDeviceIndex(device_index, device_type, device_history);
        }
    }
}

char **
MediaHistoryManager::getEmuHistoryVarForType(ui::MediaType type, int index)
{
    switch (type) {
        default:
            return nullptr;
        case ui::MediaType::Cassette:
            return &cassette_image_history[0];
        case ui::MediaType::Cartridge:
            return &cart_image_history[index][0];
        case ui::MediaType::Floppy:
            return &fdd_image_history[index][0];
        case ui::MediaType::Optical:
            return &cdrom[index].image_history[0];
        case ui::MediaType::Zip:
            return &zip_drives[index].image_history[0];
        case ui::MediaType::Mo:
            return &mo_drives[index].image_history[0];
    }
}

device_index_list_t &
MediaHistoryManager::deduplicateList(device_index_list_t &device_history, const QVector<QString> &filenames)
{
    QVector<QString> items_to_delete;
    for (auto &list_item_path : device_history) {
        if (list_item_path.isEmpty()) {
            continue;
        }
        for (const auto &path_to_check : filenames) {
            if (path_to_check.isEmpty()) {
                continue;
            }
            QString adjusted_path = pathAdjustSingle(path_to_check);
            int     match         = QString::localeAwareCompare(list_item_path, adjusted_path);
            if (match == 0) {
                items_to_delete.append(list_item_path);
            }
        }
    }
    // Remove by name rather than index because the index would change
    // after each removal
    for (const auto &path : items_to_delete) {
        device_history.removeAll(path);
    }
    return device_history;
}

void
MediaHistoryManager::addImageToHistory(int index, ui::MediaType type, const QString &image_name, const QString &new_image_name)
{
    device_index_list_t device_history = getHistoryListForDeviceIndex(index, type);
    QVector<QString>    files_to_check;

    files_to_check.append(image_name);
    files_to_check.append(new_image_name);
    device_history = deduplicateList(device_history, files_to_check);

    if (!image_name.isEmpty()) {
        device_history.push_front(image_name);
    }

    // Pop any extras
    if (device_history.size() > MAX_PREV_IMAGES) {
        device_history.pop_back();
    }

    // Fill in missing, if any
    int missing = MAX_PREV_IMAGES - device_history.size();
    if (missing) {
        for (int i = 0; i < missing; i++) {
            device_history.push_back(QString());
        }
    }

    device_history = removeMissingImages(device_history);
    device_history = pathAdjustFull(device_history);

    setHistoryListForDeviceIndex(index, type, device_history);
    serializeImageHistoryType(type);
}

QString
MediaHistoryManager::mediaTypeToString(ui::MediaType type)
{
    QMetaEnum qme = QMetaEnum::fromType<ui::MediaType>();
    return qme.valueToKey(static_cast<int>(type));
}

QString
MediaHistoryManager::pathAdjustSingle(QString checked_path)
{
    QString   current_usr_path = getUsrPath();
    QFileInfo file_info(checked_path);
    if (file_info.filePath().isEmpty() || current_usr_path.isEmpty() || file_info.isRelative()) {
        return checked_path;
    }
    return checked_path;
}

device_index_list_t &
MediaHistoryManager::pathAdjustFull(device_index_list_t &device_history)
{
    for (auto &checked_path : device_history) {
        checked_path = pathAdjustSingle(checked_path);
    }
    return device_history;
}
QString
MediaHistoryManager::getUsrPath()
{
    QString current_usr_path(usr_path);
    // Ensure `usr_path` has a trailing slash
    return current_usr_path.endsWith("/") ? current_usr_path : current_usr_path.append("/");
}
device_index_list_t &
MediaHistoryManager::removeMissingImages(device_index_list_t &device_history)
{
    for (auto &checked_path : device_history) {
        QFileInfo file_info(checked_path);
        if (file_info.filePath().isEmpty()) {
            continue;
        }

        char *p = checked_path.toUtf8().data();
        char temp[MAX_IMAGE_PATH_LEN -1] = { 0 };

        if (path_abs(p)) {
            if (strlen(p) > (MAX_IMAGE_PATH_LEN - 1))
                fatal("removeMissingImages(): strlen(p) > 2047\n");
            else
                snprintf(temp, (MAX_IMAGE_PATH_LEN - 1), "%s", p);
        } else
            snprintf(temp, (MAX_IMAGE_PATH_LEN - 1), "%s%s%s", usr_path,
                     path_get_slash(usr_path), p);
        path_normalize(temp);

        QString qstr = QString::fromUtf8(temp);
        QFileInfo new_fi(qstr);

        if ((new_fi.filePath().left(8) != "ioctl://") && !new_fi.exists()) {
            qWarning("Image file %s does not exist - removing from history", qPrintable(new_fi.filePath()));
            checked_path = "";
        }
    }
    return device_history;
}

void
MediaHistoryManager::initializeImageHistory()
{
    auto initial_master_list = getMasterList();
    setMasterList(blankImageHistory(initial_master_list));
}

const master_list_t &
MediaHistoryManager::getMasterList() const
{
    return master_list;
}

void
MediaHistoryManager::setMasterList(const master_list_t &masterList)
{
    master_list = masterList;
}

void
MediaHistoryManager::clearImageHistory()
{
    initializeImageHistory();
    serializeAllImageHistory();
}

} // ui