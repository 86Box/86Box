#pragma once

#include <memory>
#include <QObject>
#include <QMap>
#include "qt_mediahistorymanager.hpp"

extern "C" {
#include <86box/86box.h>
}
class QMenu;

class MediaMenu : public QObject {
    Q_OBJECT
public:
    MediaMenu(QWidget *parent);

    void refresh(QMenu *parentMenu);

    // because some 86box C-only code needs to call rdisk and
    // mo eject directly
    static std::shared_ptr<MediaMenu> ptr;

    void cassetteNewImage();
    void cassetteSelectImage(bool wp);
    void cassetteMount(const QString &filename, bool wp);
    void cassetteMenuSelect(int slot);
    void cassetteEject();
    void cassetteUpdateMenu();

    void cartridgeSelectImage(int i);
    void cartridgeMount(int i, const QString &filename);
    void cartridgeEject(int i);
    void cartridgeMenuSelect(int index, int slot);
    void cartridgeUpdateMenu(int i);

    void floppyNewImage(int i);
    void floppySelectImage(int i, bool wp);
    void floppyMount(int i, const QString &filename, bool wp);
    void floppyEject(int i);
    void floppyMenuSelect(int index, int slot);
    void floppyExportTo86f(int i);
    void floppyUpdateMenu(int i);

    void cdromMute(int i);
    void cdromMount(int i, int dir, const QString &arg);
    void cdromMount(int i, const QString &filename);
    void cdromEject(int i);
    void cdromReload(int index, int slot);
    void updateImageHistory(int index, int slot, ui::MediaType type);
    void clearImageHistory();
    void cdromUpdateMenu(int i);

    void rdiskNewImage(int i);
    void rdiskSelectImage(int i, bool wp);
    void rdiskMount(int i, const QString &filename, bool wp);
    void rdiskEject(int i);
    void rdiskReloadPrev(int i);
    void rdiskReload(int index, int slot);
    void rdiskUpdateMenu(int i);

    void moNewImage(int i);
    void moSelectImage(int i, bool wp);
    void moMount(int i, const QString &filename, bool wp);
    void moEject(int i);
    void moReloadPrev(int i);
    void moReload(int index, int slot);
    void moUpdateMenu(int i);

    void nicConnect(int i);
    void nicDisconnect(int i);
    void nicUpdateMenu(int i);

public slots:
    void cdromUpdateUi(int i);

signals:
    void onCdromUpdateUi(int i);

private:
    QWidget *parentWidget = nullptr;

    QMenu             *cassetteMenu = nullptr;
    QMap<int, QMenu *> cartridgeMenus;
    QMap<int, QMenu *> floppyMenus;
    QMap<int, QMenu *> cdromMenus;
    QMap<int, QMenu *> rdiskMenus;
    QMap<int, QMenu *> moMenus;
    QMap<int, QMenu *> netMenus;

    QString                 getMediaOpenDirectory();
    ui::MediaHistoryManager mhm;

    const QByteArray driveLetters = QByteArrayLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");

    int cassetteRecordPos;
    int cassettePlayPos;
    int cassetteRewindPos;
    int cassetteFastFwdPos;
    int cassetteEjectPos;
    int cassetteImageHistoryPos[MAX_PREV_IMAGES];

    int cartridgeEjectPos;
    int cartridgeImageHistoryPos[MAX_PREV_IMAGES];

    int floppyExportPos;
    int floppyEjectPos;
    int floppyImageHistoryPos[MAX_PREV_IMAGES];

    int cdromMutePos;
    int cdromEjectPos;
    int cdromImageHistoryPos[MAX_PREV_IMAGES];

    int rdiskEjectPos;
    int rdiskImageHistoryPos[MAX_PREV_IMAGES];

    int moEjectPos;
    int moImageHistoryPos[MAX_PREV_IMAGES];

    int netDisconnPos;

    friend class MachineStatus;
};
