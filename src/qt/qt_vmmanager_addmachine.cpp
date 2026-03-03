/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager add machine wizard
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "qt_vmmanager_addmachine.hpp"

extern "C" {
#include <86box/86box.h>
}

// Implementation note: There are several classes in this file:
// One for the main Wizard class and one for each page of the wizard

VMManagerAddMachine::
    VMManagerAddMachine(QWidget *parent)
    : QWizard(parent)
{
    setPage(Page_Intro, new IntroPage);
    setPage(Page_WithExistingConfig, new WithExistingConfigPage);
    setPage(Page_NameAndLocation, new NameAndLocationPage);
    setPage(Page_Conclusion, new ConclusionPage);

#ifndef Q_OS_MACOS
    setWizardStyle(ModernStyle);
    setPixmap(LogoPixmap, QPixmap(":assets/addvm-logo.png"));
#else
    setWizardStyle(MacStyle);
    setPixmap(BackgroundPixmap, QPixmap(":/assets/86box-wizard.png"));
#endif

    // Wizard wants to resize based on image. This keeps the size
#ifdef Q_OS_WINDOWS
    setMinimumSize(QSize(550, size().height()));
    setMaximumSize(QSize(550, size().height()));
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
#else
    setMinimumSize(size());
#endif
    setOption(HaveHelpButton, false);

    setWindowTitle(tr("Add new system wizard"));
}

IntroPage::
    IntroPage(QWidget *parent)
{
    setTitle(tr("Introduction"));

    setPixmap(QWizard::WatermarkPixmap, QPixmap(":assets/addvm-watermark.png"));

    topLabel = new QLabel(tr("This will help you add a new system to 86Box."));
    // topLabel = new QLabel(tr("This will help you add a new system to 86Box.\n\n Choose \"New configuration\" if you'd like to create a new machine.\n\nChoose \"Use existing configuration\" if you'd like to paste in an existing configuration from elsewhere."));
    topLabel->setWordWrap(true);

    newConfigRadioButton = new QRadioButton(tr("New configuration"));
    // auto newDescription = new QLabel(tr("Choose this option to start with a fresh configuration."));
    existingConfigRadioButton = new QRadioButton(tr("Use existing configuration"));
    // auto existingDescription = new QLabel(tr("Use this option if you'd like to paste in the configuration file from an existing system."));
    newConfigRadioButton->setChecked(true);

    const auto layout = new QVBoxLayout();
    layout->addWidget(topLabel);
    layout->addWidget(newConfigRadioButton);
    // layout->addWidget(newDescription);
    layout->addWidget(existingConfigRadioButton);
    // layout->addWidget(existingDescription);

    setLayout(layout);
}

int
IntroPage::nextId() const
{
    if (newConfigRadioButton->isChecked()) {
        return VMManagerAddMachine::Page_NameAndLocation;
    } else {
        return VMManagerAddMachine::Page_WithExistingConfig;
    }
}

WithExistingConfigPage::
    WithExistingConfigPage(QWidget *parent)
{
    setTitle(tr("Use existing configuration"));
    setSubTitle(tr("Paste the contents of the existing configuration file into the box below."));

    existingConfiguration    = new QPlainTextEdit();
    const auto monospaceFont = new QFont();
#ifdef Q_OS_WINDOWS
    monospaceFont->setFamily("Consolas");
#elif defined(Q_OS_MACOS)
    monospaceFont->setFamily("Menlo");
#else
    monospaceFont->setFamily("Monospace");
#endif
    monospaceFont->setStyleHint(QFont::Monospace);
    monospaceFont->setFixedPitch(true);
    existingConfiguration->setFont(*monospaceFont);
    connect(existingConfiguration, &QPlainTextEdit::textChanged, this, &WithExistingConfigPage::completeChanged);
    registerField("existingConfiguration*", this, "configuration");

    const auto layout = new QVBoxLayout();
    layout->addWidget(existingConfiguration);
    const auto loadFileButton = new QPushButton();
    const auto loadFileLabel  = new QLabel(tr("Load configuration from file"));
    const auto hLayout        = new QHBoxLayout();
    loadFileButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
    loadFileButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    connect(loadFileButton, &QPushButton::clicked, this, &WithExistingConfigPage::chooseExistingConfigFile);
    hLayout->addWidget(loadFileButton);
    hLayout->addWidget(loadFileLabel);
    layout->addLayout(hLayout);
    setLayout(layout);
}

void
WithExistingConfigPage::chooseExistingConfigFile()
{
    const auto startDirectory     = QString(vmm_path);
    const auto selectedConfigFile = QFileDialog::getOpenFileName(this, tr("Choose configuration file"),
                                                                 startDirectory,
                                                                 tr("86Box configuration files (86box.cfg)"));
    // Empty value means the dialog was canceled
    if (!selectedConfigFile.isEmpty()) {
        QFile configFile(selectedConfigFile);
        if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(this, tr("Configuration read failed"), tr("Unable to open the selected configuration file for reading: %1").arg(configFile.errorString()));
            return;
        }
        const QString configFileContents = configFile.readAll();
        existingConfiguration->setPlainText(configFileContents);
        configFile.close();
        emit completeChanged();
    }
}

QString
WithExistingConfigPage::configuration() const
{
    return existingConfiguration->toPlainText();
}
void
WithExistingConfigPage::setConfiguration(const QString &configuration)
{
    if (configuration != existingConfiguration->toPlainText()) {
        existingConfiguration->setPlainText(configuration);
        emit configurationChanged(configuration);
    }
}

int
WithExistingConfigPage::nextId() const
{
    return VMManagerAddMachine::Page_NameAndLocation;
}

bool
WithExistingConfigPage::isComplete() const
{
    return !existingConfiguration->toPlainText().isEmpty();
}

NameAndLocationPage::
    NameAndLocationPage(QWidget *parent)
{
#ifdef CUSTOM_SYSTEM_LOCATION
    setTitle(tr("System name and location"));

#    if defined(_WIN32)
    dirValidate = QRegularExpression(R"(^[^\\/:*?"<>|]+$)");
#    elif defined(__APPLE__)
    dirValidate = QRegularExpression(R"(^[^/:]+$)");
#    else
    dirValidate = QRegularExpression(R"(^[^/]+$)");
#    endif

    setSubTitle(tr("Enter the name of the system and choose the location"));
#else
    setTitle(tr("System name"));
    setSubTitle(tr("Enter the name of the system"));
#endif

    const auto chooseDirectoryButton = new QPushButton();
    chooseDirectoryButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));

    const auto systemNameLabel = new QLabel(tr("System name:"));
    systemName                 = new QLineEdit();
    // Special event filter to override enter key
    systemName->installEventFilter(this);
    registerField("systemName*", systemName);
    systemNameValidation = new QLabel();

#ifdef CUSTOM_SYSTEM_LOCATION
    const auto systemLocationLabel = new QLabel(tr("System location:"));
    systemLocation                 = new QLineEdit();
    systemLocation->setText(QDir::toNativeSeparators(vmm_path));
    registerField("systemLocation*", systemLocation);
    systemLocationValidation = new QLabel();
    systemLocationValidation->setWordWrap(true);
#endif

    const auto displayNameLabel = new QLabel(tr("Display name (optional):"));
    displayName                 = new QLineEdit();
    // Special event filter to override enter key
    displayName->installEventFilter(this);
    registerField("displayName*", displayName);

    const auto layout = new QGridLayout();
    // Spacer row
    layout->setRowMinimumHeight(1, 20);
    layout->addWidget(systemNameLabel, 2, 0);
    layout->addWidget(systemName, 2, 1);
    // Validation text, appears only as necessary
    layout->addWidget(systemNameValidation, 3, 0, 1, -1);
    // Set height on validation because it may not always be present
    layout->setRowMinimumHeight(3, 20);

#ifdef CUSTOM_SYSTEM_LOCATION
    // Another spacer
    layout->setRowMinimumHeight(4, 20);
    layout->addWidget(systemLocationLabel, 5, 0);
    layout->addWidget(systemLocation, 5, 1);
    layout->addWidget(chooseDirectoryButton, 5, 2);
    // Validation text
    layout->addWidget(systemLocationValidation, 6, 0, 1, -1);
    layout->setRowMinimumHeight(6, 20);
#endif

    // Another spacer
    layout->setRowMinimumHeight(7, 20);
    layout->addWidget(displayNameLabel, 8, 0);
    layout->addWidget(displayName, 8, 1);

    setLayout(layout);

#ifdef CUSTOM_SYSTEM_LOCATION
    connect(chooseDirectoryButton, &QPushButton::clicked, this, &NameAndLocationPage::chooseDirectoryLocation);
#endif
}

int
NameAndLocationPage::nextId() const
{
    return VMManagerAddMachine::Page_Conclusion;
}

#ifdef CUSTOM_SYSTEM_LOCATION
void
NameAndLocationPage::chooseDirectoryLocation()
{
    const auto directory = QFileDialog::getExistingDirectory(this, "Choose directory", QDir(vmm_path).path());
    systemLocation->setText(QDir::toNativeSeparators(directory));
    emit completeChanged();
}
#endif
bool
NameAndLocationPage::isComplete() const
{
    bool nameValid = false;
#ifdef CUSTOM_SYSTEM_LOCATION
    bool locationValid = false;
#endif
    // return true if complete
    if (systemName->text().isEmpty()) {
        systemNameValidation->setText(tr("Please enter a system name"));
#ifdef CUSTOM_SYSTEM_LOCATION
    } else if (!systemName->text().contains(dirValidate)) {
        systemNameValidation->setText(tr("System name cannot contain certain characters"));
    } else if (const QDir newDir = QDir::cleanPath(systemLocation->text() + "/" + systemName->text()); newDir.exists()) {
#else
    } else if (const QDir newDir = QDir::cleanPath(QString(vmm_path) + "/" + systemName->text()); newDir.exists()) {
#endif
        systemNameValidation->setText(tr("System name already exists"));
    } else {
        systemNameValidation->clear();
        nameValid = true;
    }

#ifdef CUSTOM_SYSTEM_LOCATION
    if (systemLocation->text().isEmpty()) {
        systemLocationValidation->setText(tr("Please enter a directory for the system"));
    } else if (const auto dir = QDir(systemLocation->text()); !dir.exists()) {
        systemLocationValidation->setText(tr("Directory does not exist"));
    } else {
        systemLocationValidation->setText(tr("A new directory for the system will be created in the selected directory above"));
        locationValid = true;
    }

    return nameValid && locationValid;
#else
    return nameValid;
#endif
}
bool
NameAndLocationPage::eventFilter(QObject *watched, QEvent *event)
{
    // Override the enter key to hit the next wizard button
    // if the validator (isComplete) is satisfied
    if (event->type() == QEvent::KeyPress) {
        const auto keyEvent = dynamic_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) {
            // Only advance if the validator is satisfied (isComplete)
            if (const auto wizard = qobject_cast<QWizard *>(this->wizard())) {
                if (wizard->currentPage()->isComplete()) {
                    wizard->next();
                }
            }
            // Discard the key event
            return true;
        }
    }
    return QWizardPage::eventFilter(watched, event);
}

ConclusionPage::
    ConclusionPage(QWidget *parent)
{
    setTitle(tr("Complete"));

    setPixmap(QWizard::WatermarkPixmap, QPixmap(":assets/addvm-watermark.png"));

    topLabel = new QLabel(tr("The wizard will now launch the configuration for the new system."));
    topLabel->setWordWrap(true);

    const auto systemNameLabel = new QLabel(tr("System name:"));
    systemNameLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    systemName = new QLabel();
    systemName->setWordWrap(true);
#ifdef CUSTOM_SYSTEM_LOCATION
    const auto systemLocationLabel = new QLabel(tr("System location:"));
    systemLocationLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    systemLocation = new QLabel();
    systemLocation->setWordWrap(true);
#endif

    displayNameLabel = new QLabel(tr("Display name:"));
    displayNameLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    displayName = new QLabel();
    displayName->setWordWrap(true);

    const auto layout = new QGridLayout();
    layout->addWidget(topLabel, 0, 0, 1, -1);
    layout->setRowMinimumHeight(1, 20);
    layout->addWidget(systemNameLabel, 2, 0);
    layout->addWidget(systemName, 2, 1);
#ifdef CUSTOM_SYSTEM_LOCATION
    layout->addWidget(systemLocationLabel, 3, 0);
    layout->addWidget(systemLocation, 3, 1);
#endif
    layout->addWidget(displayNameLabel, 4, 0);
    layout->addWidget(displayName, 4, 1);

    setLayout(layout);
}

// initializePage() runs after the page has been created with the constructor
void
ConclusionPage::initializePage()
{
#ifdef CUSTOM_SYSTEM_LOCATION
    const auto finalPath  = QDir::cleanPath(field("systemLocation").toString() + "/" + field("systemName").toString());
    const auto nativePath = QDir::toNativeSeparators(finalPath);
#endif
    const auto systemNameDisplay  = field("systemName").toString();
    const auto displayNameDisplay = field("displayName").toString();

    systemName->setText(systemNameDisplay);
#ifdef CUSTOM_SYSTEM_LOCATION
    systemLocation->setText(nativePath);
#endif
    if (!displayNameDisplay.isEmpty()) {
        displayNameLabel->setVisible(true);
        displayName->setVisible(true);
        displayName->setText(displayNameDisplay);
    } else {
        displayNameLabel->setVisible(false);
        displayName->setVisible(false);
    }
}
