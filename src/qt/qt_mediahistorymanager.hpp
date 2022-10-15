/*
* 86Box	A hypervisor and IBM PC system emulator that specializes in
*		running old operating systems and software designed for IBM
*		PC systems and compatibles from 1981 through fairly recent
*		system designs based on the PCI bus.
*
*		This file is part of the 86Box distribution.
*
*		Header for the media history management module
*
*
*
* Authors:	cold-brewed
*
*		Copyright 2022 The 86Box development team
*/

#ifndef QT_MEDIAHISTORYMANAGER_HPP
#define QT_MEDIAHISTORYMANAGER_HPP

#include <QString>
#include <QWidget>
#include <QVector>

#include <initializer_list>

extern "C" {
#include <86box/86box.h>
}

// This macro helps give us the required `qHash()` function in order to use the
// enum as a hash key
#define QHASH_FOR_CLASS_ENUM(T) \
inline uint qHash(const T &t, uint seed) { \
    return ::qHash(static_cast<typename std::underlying_type<T>::type>(t), seed); \
}

typedef QVector<QString>             device_index_list_t;
typedef QHash<int, QVector<QString>> device_media_history_t;


namespace ui {
    Q_NAMESPACE
    
    enum class MediaType {
        Floppy,
        Optical,
        Zip,
        Mo,
        Cassette
    };
    // This macro allows us to do a reverse lookup of the enum with `QMetaEnum`
    Q_ENUM_NS(MediaType)
    
    QHASH_FOR_CLASS_ENUM(MediaType)

    typedef QHash<ui::MediaType, device_media_history_t> master_list_t;

    // Used to iterate over all supported types when preparing data structures
    // Also useful to indicate which types support history
    static const MediaType AllSupportedMediaHistoryTypes[] = {
        MediaType::Optical,
        MediaType::Floppy,
    };

    class MediaHistoryManager {

    public:
        MediaHistoryManager();
        virtual ~MediaHistoryManager();

        // Get the image name for a particular slot,
        // index, and type combination
        QString getImageForSlot(int index, int slot, ui::MediaType type);

        // Add an image to history
        void addImageToHistory(int index, ui::MediaType type, const QString& image_name, const QString& new_image_name);

        // Convert the enum value to a string
        static QString mediaTypeToString(ui::MediaType type);

        // Clear out the image history
        void clearImageHistory();


    private:
        int max_images = MAX_PREV_IMAGES;

        // Main hash of hash of vector of strings
        master_list_t       master_list;
        [[nodiscard]] const master_list_t &getMasterList() const;
        void                 setMasterList(const master_list_t &masterList);

        device_index_list_t index_list, empty_device_index_list;

        // Return a blank, initialized image history list
        master_list_t &blankImageHistory(master_list_t &initialized_master_list) const;

        // Initialize the image history
        void initializeImageHistory();

        // Max number of devices supported by media type
        static int maxDevicesSupported(ui::MediaType type);

        // Serialize the data back into the C array
        // on the emu side
        void serializeImageHistoryType(ui::MediaType type);
        void serializeAllImageHistory();

        // Deserialize the data from C array on the emu side
        // for the ui side
        void deserializeImageHistoryType(ui::MediaType type);
        void deserializeAllImageHistory();

        // Get emu history variable for a device type
        static char** getEmuHistoryVarForType(ui::MediaType type, int index);

        // Get or set the history for a specific device/index combo
        const device_index_list_t &getHistoryListForDeviceIndex(int index, ui::MediaType type);
        void setHistoryListForDeviceIndex(int index, ui::MediaType type, device_index_list_t history_list);

        // Remove missing image files from history list
        static device_index_list_t &removeMissingImages(device_index_list_t &device_history);

        // If an absolute path is contained within `usr_path`, convert to a relative path
        static device_index_list_t &pathAdjustFull(device_index_list_t &device_history);
        static QString pathAdjustSingle(QString checked_path);

        // Deduplicate history entries
        static device_index_list_t &deduplicateList(device_index_list_t &device_history, const QVector<QString>& filenames);
        void initialDeduplication();

        // Gets the `usr_path` from the emu side and appends a
        // trailing slash if necessary
        static QString getUsrPath();
    };

} // ui

#endif // QT_MEDIAHISTORYMANAGER_HPP
