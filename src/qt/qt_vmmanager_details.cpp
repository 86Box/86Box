/*
* 86Box	A hypervisor and IBM PC system emulator that specializes in
*		running old operating systems and software designed for IBM
*		PC systems and compatibles from 1981 through fairly recent
*		system designs based on the PCI bus.
*
*		This file is part of the 86Box distribution.
*
*		86Box VM manager system details module
*
*
*
* Authors:	cold-brewed
*
*		Copyright 2024 cold-brewed
*/

#include <QApplication>
#include <QDebug>
#include <QStyle>

#include "qt_vmmanager_details.hpp"
#include "ui_qt_vmmanager_details.h"

#ifdef Q_OS_WINDOWS
extern bool windows_is_light_theme();
#endif

VMManagerDetails::VMManagerDetails(QWidget *parent) :
    QWidget(parent), ui(new Ui::VMManagerDetails) {
    ui->setupUi(this);

    const auto leftColumnLayout = qobject_cast<QVBoxLayout*>(ui->leftColumn->layout());

    // Each section here gets its own VMManagerDetailSection, named in the constructor.
    // When a system is selected in the list view it is updated through this object
    // See updateData() for the implementation

    systemSection = new VMManagerDetailSection(tr("System", "Header for System section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(systemSection);
    // These horizontal lines are used for the alternate layout which may possibly
    // be a preference one day.
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    videoSection = new VMManagerDetailSection(tr("Display", "Header for Display section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(videoSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    storageSection = new VMManagerDetailSection(tr("Storage", "Header for Storage section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(storageSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    audioSection = new VMManagerDetailSection(tr("Audio", "Header for Audio section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(audioSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    networkSection = new VMManagerDetailSection(tr("Network", "Header for Network section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(networkSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    inputSection = new VMManagerDetailSection(tr("Input Devices", "Header for Input section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(inputSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    portsSection = new VMManagerDetailSection(tr("Ports", "Header for Input section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(portsSection);

    // This is like adding a spacer
    leftColumnLayout->addStretch();

    // Event filter for the notes to save when it loses focus
    ui->notesTextEdit->installEventFilter(this);

    // Default screenshot label and thumbnail (image inside the label) sizes
    screenshotThumbnailSize = QSize(240, 160);

    // Set the icons for the screenshot navigation buttons
    ui->screenshotNext->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowRight));
    ui->screenshotPrevious->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowLeft));
    ui->screenshotNextTB->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowRight));
    ui->screenshotPreviousTB->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowLeft));
    // Disabled by default
    ui->screenshotNext->setEnabled(false);
    ui->screenshotPrevious->setEnabled(false);
    ui->screenshotNextTB->setEnabled(false);
    ui->screenshotPreviousTB->setEnabled(false);
    // Connect their signals
    connect(ui->screenshotNext, &QPushButton::clicked, this, &VMManagerDetails::nextScreenshot);
    connect(ui->screenshotNextTB, &QToolButton::clicked, this, &VMManagerDetails::nextScreenshot);
    connect(ui->screenshotPreviousTB, &QToolButton::clicked, this, &VMManagerDetails::previousScreenshot);
    connect(ui->screenshotPrevious, &QPushButton::clicked, this, &VMManagerDetails::previousScreenshot);
    // These push buttons can be taken out if the tool buttons stay
    ui->screenshotNext->setVisible(false);
    ui->screenshotPrevious->setVisible(false);
    QString toolButtonStyleSheet;
    // Simple method to try and determine if light mode is enabled
#ifdef Q_OS_WINDOWS
    const bool lightMode = windows_is_light_theme();
#else
    const bool lightMode = QApplication::palette().window().color().value() > QApplication::palette().windowText().color().value();
#endif
    if (lightMode) {
        toolButtonStyleSheet = "QToolButton {background: transparent; border: none; padding: 5px} QToolButton:hover {background: palette(midlight)} QToolButton:pressed {background: palette(mid)}";
    } else {
#ifndef Q_OS_WINDOWS
        toolButtonStyleSheet = "QToolButton {background: transparent; border: none; padding: 5px} QToolButton:hover {background: palette(dark)} QToolButton:pressed {background: palette(mid)}";
#else
        toolButtonStyleSheet = "QToolButton {padding: 5px}";
#endif
    }
    ui->ssNavTBHolder->setStyleSheet(toolButtonStyleSheet);

    // Experimenting
    startPauseButton = new QToolButton();
    startPauseButton->setIcon(QIcon(":/menuicons/qt/icons/run.ico"));
    startPauseButton->setAutoRaise(true);
    ui->toolButtonHolder->setStyleSheet(toolButtonStyleSheet);
    resetButton = new QToolButton();
    resetButton->setIcon(QIcon(":/menuicons/qt/icons/hard_reset.ico"));
    stopButton = new QToolButton();
    stopButton->setIcon(QIcon(":/menuicons/qt/icons/acpi_shutdown.ico"));
    configureButton = new QToolButton();
    configureButton->setIcon(QIcon(":/menuicons/qt/icons/settings.ico"));

    ui->toolButtonHolder->layout()->addWidget(configureButton);
    ui->toolButtonHolder->layout()->addWidget(resetButton);
    ui->toolButtonHolder->layout()->addWidget(stopButton);
    ui->toolButtonHolder->layout()->addWidget(startPauseButton);

    sysconfig = new VMManagerSystem();
}

VMManagerDetails::~VMManagerDetails() {
    delete ui;
}

void
VMManagerDetails::updateData(VMManagerSystem *passed_sysconfig) {

    // Set the scrollarea background but also set the scroll bar to none. Otherwise it will also
    // set the scrollbar background to the same.
#ifdef Q_OS_WINDOWS
    extern bool windows_is_light_theme();
    if (windows_is_light_theme())
#endif
    {
        ui->scrollArea->setStyleSheet("QWidget {background-color: palette(light)} QScrollBar{ background-color: none }");
        ui->systemLabel->setStyleSheet("background-color: palette(midlight);");
    }
    // Margins are a little different on macos
#ifdef Q_OS_MACOS
    ui->systemLabel->setMargin(15);
#else
    ui->systemLabel->setMargin(10);
#endif

    // disconnect old signals before assigning the passed systemconfig object
    disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
    disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
    disconnect(resetButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::restartButtonPressed);
    disconnect(stopButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::shutdownForceButtonPressed);
    disconnect(configureButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::launchSettings);

    sysconfig = passed_sysconfig;
    connect(resetButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::restartButtonPressed);
    connect(stopButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::shutdownForceButtonPressed);
    connect(configureButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::launchSettings);

    bool running = sysconfig->getProcessStatus() == VMManagerSystem::ProcessStatus::Running ||
        sysconfig->getProcessStatus() == VMManagerSystem::ProcessStatus::RunningWaiting;
    if(running) {
        startPauseButton->setIcon(QIcon(":/menuicons/qt/icons/pause.ico"));
        connect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
    } else {
        startPauseButton->setIcon(QIcon(":/menuicons/qt/icons/run.ico"));
        connect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
    }

    // Each detail section here has its own VMManagerDetailSection.
    // When a system is selected in the list view it is updated here, through this object:
    // * First you clear it with VMManagerDetailSection::clear()
    // * Then you add each line with VMManagerDetailSection::addSection()

    // System
    systemSection->clear();
    systemSection->addSection("Machine", passed_sysconfig->getDisplayValue(Display::Name::Machine));
    systemSection->addSection("CPU", passed_sysconfig->getDisplayValue(Display::Name::CPU));
    systemSection->addSection("Memory", passed_sysconfig->getDisplayValue(Display::Name::Memory));

    // Video
    videoSection->clear();
    videoSection->addSection("Video", passed_sysconfig->getDisplayValue(Display::Name::Video));
    if(!passed_sysconfig->getDisplayValue(Display::Name::Voodoo).isEmpty()) {
        videoSection->addSection("Voodoo", passed_sysconfig->getDisplayValue(Display::Name::Voodoo));
    }

    // Disks
    storageSection->clear();
    storageSection->addSection("Disks", passed_sysconfig->getDisplayValue(Display::Name::Disks));
    storageSection->addSection("Floppy", passed_sysconfig->getDisplayValue(Display::Name::Floppy));
    storageSection->addSection("CD-ROM", passed_sysconfig->getDisplayValue(Display::Name::CD));
    storageSection->addSection("SCSI", passed_sysconfig->getDisplayValue(Display::Name::SCSIController));

    // Audio
    audioSection->clear();
    audioSection->addSection("Audio", passed_sysconfig->getDisplayValue(Display::Name::Audio));
    audioSection->addSection("MIDI Out", passed_sysconfig->getDisplayValue(Display::Name::MidiOut));

    // Network
    networkSection->clear();
    networkSection->addSection("NIC", passed_sysconfig->getDisplayValue(Display::Name::NIC));

    // Input
    inputSection->clear();
    inputSection->addSection(tr("Mouse"), passed_sysconfig->getDisplayValue(Display::Name::Mouse));
    inputSection->addSection(tr("Joystick"), passed_sysconfig->getDisplayValue(Display::Name::Joystick));

    // Ports
    portsSection->clear();
    portsSection->addSection(tr("Serial Ports"), passed_sysconfig->getDisplayValue(Display::Name::Serial));
    portsSection->addSection(tr("Parallel Ports"), passed_sysconfig->getDisplayValue(Display::Name::Parallel));

    // Disable screenshot navigation buttons by default
    ui->screenshotNext->setEnabled(false);
    ui->screenshotPrevious->setEnabled(false);
    ui->screenshotNextTB->setEnabled(false);
    ui->screenshotPreviousTB->setEnabled(false);

    // Different actions are taken depending on the existence and number of screenshots
    screenshots = passed_sysconfig->getScreenshots();
    if (!screenshots.empty()) {
        ui->screenshot->setFrameStyle(QFrame::NoFrame);
        ui->screenshot->setEnabled(true);
        if(screenshots.size() > 1) {
            ui->screenshotNext->setEnabled(true);
            ui->screenshotPrevious->setEnabled(true);
            ui->screenshotNextTB->setEnabled(true);
            ui->screenshotPreviousTB->setEnabled(true);
        }
        if(QFileInfo::exists(screenshots.last().filePath())) {
            screenshotIndex = screenshots.size() - 1;
            const QPixmap pic(screenshots.at(screenshotIndex).filePath());
            ui->screenshot->setPixmap(pic.scaled(240, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    } else {
        ui->screenshotNext->setEnabled(false);
        ui->screenshotPrevious->setEnabled(false);
        ui->screenshotNextTB->setEnabled(false);
        ui->screenshotPreviousTB->setEnabled(false);
        ui->screenshot->setPixmap(QString());
        ui->screenshot->setFixedSize(240, 160);
        ui->screenshot->setFrameStyle(QFrame::Box | QFrame::Sunken);
        ui->screenshot->setText(tr("No screenshot"));
        ui->screenshot->setEnabled(false);
        ui->screenshot->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    }

    ui->systemLabel->setText(passed_sysconfig->displayName);
    ui->statusLabel->setText(sysconfig->process->processId() == 0 ?
        tr("Not running") :
        QString("%1: PID %2").arg(tr("Running"), QString::number(sysconfig->process->processId())));
    ui->notesTextEdit->setPlainText(passed_sysconfig->notes);

    disconnect(sysconfig->process, &QProcess::stateChanged, this, &VMManagerDetails::updateProcessStatus);
    connect(sysconfig->process, &QProcess::stateChanged, this, &VMManagerDetails::updateProcessStatus);

    disconnect(sysconfig, &VMManagerSystem::windowStatusChanged, this, &VMManagerDetails::updateWindowStatus);
    connect(sysconfig, &VMManagerSystem::windowStatusChanged, this, &VMManagerDetails::updateWindowStatus);

    disconnect(sysconfig, &VMManagerSystem::clientProcessStatusChanged, this, &VMManagerDetails::updateProcessStatus);
    connect(sysconfig, &VMManagerSystem::clientProcessStatusChanged, this, &VMManagerDetails::updateProcessStatus);

    updateProcessStatus();
}

void
VMManagerDetails::updateProcessStatus() {
    const bool running = sysconfig->process->state() == QProcess::ProcessState::Running;
    QString status_text = running ?
        QString("%1: PID %2").arg(tr("Running"), QString::number(sysconfig->process->processId())) :
        tr("Not running");
    status_text.append(sysconfig->window_obscured ? QString(" (%1)").arg(tr("waiting")) : "");
    ui->statusLabel->setText(status_text);
    resetButton->setEnabled(running);
    stopButton->setEnabled(running);
    if(running) {
        if(sysconfig->getProcessStatus() == VMManagerSystem::ProcessStatus::Running) {
            startPauseButton->setIcon(QIcon(":/menuicons/qt/icons/pause.ico"));
        } else {
            startPauseButton->setIcon(QIcon(":/menuicons/qt/icons/run.ico"));
        }

        disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
        disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
        connect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
    } else {
        startPauseButton->setIcon(QIcon(":/menuicons/qt/icons/run.ico"));
        disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
        disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
        connect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
    }
}

void
VMManagerDetails::updateWindowStatus()
{
    qInfo("Window status changed: %i", sysconfig->window_obscured);
    updateProcessStatus();
}

QWidget *
VMManagerDetails::createHorizontalLine(const int leftSpacing, const int rightSpacing)
{
    const auto container = new QWidget;
    const auto hLayout   = new QHBoxLayout(container);

    hLayout->addSpacing(leftSpacing);

    const auto line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    hLayout->addWidget(line);
    hLayout->addSpacing(rightSpacing);
    hLayout->setContentsMargins(0, 5, 0, 5);

    return container;
}

void
VMManagerDetails::saveNotes() const
{
    sysconfig->setNotes(ui->notesTextEdit->toPlainText());
}

void
VMManagerDetails::nextScreenshot()
{
    screenshotIndex = (screenshotIndex + 1) % screenshots.size();
    const QPixmap pic(screenshots.at(screenshotIndex).filePath());
    ui->screenshot->setPixmap(pic.scaled(240, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void
VMManagerDetails::previousScreenshot()
{
    screenshotIndex = screenshotIndex == 0 ? screenshots.size() - 1 : screenshotIndex - 1;
    const QPixmap pic(screenshots.at(screenshotIndex).filePath());
    ui->screenshot->setPixmap(pic.scaled(240, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool
VMManagerDetails::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->isWidgetType() && event->type() == QEvent::FocusOut) {
        // Make sure it's the textedit
        if (const auto *textEdit = qobject_cast<QPlainTextEdit*>(watched); textEdit) {
            saveNotes();
        }
    }
    return QWidget::eventFilter(watched, event);
}

