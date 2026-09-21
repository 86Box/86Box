/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager system details module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_DETAILS_H
#define QT_VMMANAGER_DETAILS_H

#include <QWidget>
#include "qt_vmmanager_system.hpp"
// #include "qt_vmmanager_details_section.hpp"
#include "qt_vmmanager_detailsection.hpp"

QT_BEGIN_NAMESPACE
// namespace Ui { class VMManagerDetails; class CollapseButton;}
namespace Ui {
class VMManagerDetails;
}
QT_END_NAMESPACE

class VMManagerDetails : public QWidget {
    Q_OBJECT

public:
    explicit VMManagerDetails(QWidget *parent = nullptr);

    ~VMManagerDetails() override;

    void reset();

    void updateData(VMManagerSystem *passed_sysconfig);

    void updateProcessStatus();

    void updateWindowStatus();

#ifdef Q_OS_WINDOWS
    void updateStyle();
#endif

    // CollapseButton *systemCollapseButton;

#ifdef Q_OS_WINDOWS
signals:
    void styleUpdated();
#endif

private:
    Ui::VMManagerDetails *ui;
    VMManagerSystem      *sysconfig;

    VMManagerDetailSection *systemSection;
    VMManagerDetailSection *videoSection;
    VMManagerDetailSection *storageSection;
    VMManagerDetailSection *audioSection;
    VMManagerDetailSection *networkSection;
    VMManagerDetailSection *inputSection;
    VMManagerDetailSection *portsSection;
    VMManagerDetailSection *otherSection;

    QFileInfoList screenshots;
    int           screenshotIndex = 0;
    QSize         screenshotLabelSize;
    QSize         screenshotThumbnailSize;

    QToolButton *startPauseButton;
    QToolButton *resetButton;
    QToolButton *stopButton;
    QToolButton *configureButton;
    QToolButton *cadButton;

    QIcon pauseIcon;
    QIcon runIcon;

    void            updateConfig(VMManagerSystem *passed_sysconfig);
    void            updateScreenshots(VMManagerSystem *passed_sysconfig);
    static QWidget *createHorizontalLine(int leftSpacing = 25, int rightSpacing = 25);
    // QVBoxLayout *detailsLayout;
private slots:
    void saveNotes() const;
    void nextScreenshot();
    void previousScreenshot();
    void onConfigUpdated(VMManagerSystem *passed_sysconfig);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

#if 0
    CollapseButton *systemCollapseButton;
    QFrame         *systemFrame;
    CollapseButton *displayCollapseButton;
    QFrame         *displayFrame;
    CollapseButton *storageCollapseButton;
    QFrame         *storageFrame;
#endif
};

#endif // QT_VMMANAGER_DETAILS_H
