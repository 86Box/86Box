#pragma once

#include <memory>
#include <QObject>
#include <QMap>

class QMenu;

class MediaMenu : QObject
{
    Q_OBJECT
public:
    MediaMenu(QWidget* parent);

    void refresh(QMenu* parentMenu);

    // because some 86box C-only code needs to call zip and
    // mo eject directly
    static std::shared_ptr<MediaMenu> ptr;

    void cassetteNewImage();
    void cassetteSelectImage(bool wp);
    void cassetteMount(const QString& filename, bool wp);
    void cassetteEject();
    void cassetteUpdateMenu();

    void cartridgeSelectImage(int i);
    void cartridgeEject(int i);
    void cartridgeUpdateMenu(int i);

    void floppyNewImage(int i);
    void floppySelectImage(int i, bool wp);
    void floppyMount(int i, const QString& filename, bool wp);
    void floppyEject(int i);
    void floppyExportTo86f(int i);
    void floppyUpdateMenu(int i);

    void cdromMute(int i);
    void cdromMount(int i);
    void cdromEject(int i);
    void cdromReload(int i);
    void cdromUpdateMenu(int i);

    void zipNewImage(int i);
    void zipSelectImage(int i, bool wp);
    void zipMount(int i, const QString& filename, bool wp);
    void zipEject(int i);
    void zipReload(int i);
    void zipUpdateMenu(int i);

    void moNewImage(int i);
    void moSelectImage(int i, bool wp);
    void moMount(int i, const QString& filename, bool wp);
    void moEject(int i);
    void moReload(int i);
    void moUpdateMenu(int i);
private:
    QWidget* parentWidget = nullptr;

    QMenu* cassetteMenu = nullptr;
    QMap<int, QMenu*> cartridgeMenus;
    QMap<int, QMenu*> floppyMenus;
    QMap<int, QMenu*> cdromMenus;
    QMap<int, QMenu*> zipMenus;
    QMap<int, QMenu*> moMenus;

    int cassetteRecordPos;
    int cassettePlayPos;
    int cassetteRewindPos;
    int cassetteFastFwdPos;
    int cassetteEjectPos;

    int cartridgeEjectPos;

    int floppyExportPos;
    int floppyEjectPos;

    int cdromMutePos;
    int cdromEmptyPos;
    int cdromReloadPos;
    int cdromImagePos;

    int zipEjectPos;
    int zipReloadPos;

    int moEjectPos;
    int moReloadPos;

    friend class MachineStatus;
};
