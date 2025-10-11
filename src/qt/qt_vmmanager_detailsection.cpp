/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager system details section module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include "qt_vmmanager_detailsection.hpp"
#include "ui_qt_vmmanager_detailsection.h"

#include <QPushButton>
#include "qt_util.hpp"

#define HEADER_STYLESHEET_LIGHT "background-color: palette(midlight);"
#ifdef Q_OS_WINDOWS
#    define HEADER_STYLESHEET_DARK     "background-color: #616161;"
#    define BACKGROUND_STYLESHEET_DARK "background-color: #272727;"
#else
#    define HEADER_STYLESHEET_DARK "background-color: palette(mid);"
#endif

const QString VMManagerDetailSection::sectionSeparator = ";";
using namespace VMManager;

VMManagerDetailSection::
    VMManagerDetailSection(const QString &sectionName)
    : mainLayout(new QVBoxLayout())
    , buttonLayout(new QHBoxLayout())
    , frame(new QFrame())
    , ui(new Ui::DetailSection)
{
    ui->setupUi(this);

    frameGridLayout = new QGridLayout();
    // Create the collapse button, set the name and add it to the layout
    collapseButton = new CollapseButton();
    setSectionName(sectionName);
    ui->collapseButtonHolder->setContentsMargins(getMargins(MarginSection::ToolButton));

    // Simple method to try and determine if light mode is enabled on the host
#ifdef Q_OS_WINDOWS
    const bool lightMode = util::isWindowsLightTheme();
#else
    const bool lightMode = QApplication::palette().window().color().value() > QApplication::palette().windowText().color().value();
#endif
    // Alternate layout
    if (lightMode) {
        ui->collapseButtonHolder->setStyleSheet(HEADER_STYLESHEET_LIGHT);
    } else {
#ifdef Q_OS_WINDOWS
        ui->outerFrame->setStyleSheet(BACKGROUND_STYLESHEET_DARK);
#endif
        ui->collapseButtonHolder->setStyleSheet(HEADER_STYLESHEET_DARK);
    }
    const auto sectionLabel = new QLabel(sectionName);
    sectionLabel->setStyleSheet(sectionLabel->styleSheet().append("font-weight: bold;"));
    ui->collapseButtonHolder->setContentsMargins(QMargins(3, 2, 0, 2));
    ui->collapseButtonHolder->layout()->addWidget(sectionLabel);

    // ui->collapseButtonHolder->layout()->addWidget(collapseButton);
    collapseButton->setContent(ui->detailFrame);
    // Horizontal line added after the section name / button
    // const auto hLine = new QFrame();
    // hLine->setFrameShape(QFrame::HLine);
    // hLine->setFrameShadow(QFrame::Sunken);
    // hLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    // ui->collapseButtonHolder->layout()->addWidget(hLine);
    const auto hSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
    ui->collapseButtonHolder->layout()->addItem(hSpacer);
    // collapseButton->setContent(frame);
    // ui->sectionName->setVisible(false);
    setVisible(false);
}

VMManagerDetailSection::
    VMManagerDetailSection(const QVariant &varSectionName)
    : ui(new Ui::DetailSection)
{
    const auto sectionName = varSectionName.toString();

    // Initialize even though they will get wiped out
    // (to keep clang-tidy happy)
    frameGridLayout       = new QGridLayout();
    const auto outerFrame = new QFrame();
    // for the CSS
    outerFrame->setObjectName("outer_frame");
    outerFrame->setContentsMargins(QMargins(0, 0, 0, 0));
    const auto innerFrameLayout = new QVBoxLayout();

    outerFrame->setLayout(innerFrameLayout);
    auto *outerFrameLayout = new QVBoxLayout();
    outerFrameLayout->addWidget(outerFrame);
    outerFrameLayout->setContentsMargins(QMargins(0, 0, 0, 0));

    const auto buttonWidget = new QWidget(this);

    mainLayout   = new QVBoxLayout();
    buttonLayout = new QHBoxLayout();
    buttonWidget->setLayout(buttonLayout);

    collapseButton = new CollapseButton();
    setSectionName(sectionName);
    buttonLayout->setContentsMargins(getMargins(MarginSection::ToolButton));
    buttonLayout->addWidget(collapseButton);

    //    buttonLayout->addStretch();
    auto *hLine = new QFrame();
    hLine->setFrameShape(QFrame::HLine);
    hLine->setFrameShadow(QFrame::Sunken);
    hLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    buttonLayout->addWidget(hLine);

    mainLayout->addLayout(buttonLayout);

    frame = new QFrame();
    frame->setFrameShape(QFrame::Box);
    frame->setFrameStyle(QFrame::NoFrame);
    collapseButton->setContent(frame);

    mainLayout->addWidget(frame);
    innerFrameLayout->addWidget(buttonWidget);
    innerFrameLayout->addWidget(frame);
    setLayout(outerFrameLayout);
}

VMManagerDetailSection::~VMManagerDetailSection()
    = default;

void
VMManagerDetailSection::setSectionName(const QString &name)
{
    sectionName = name;
    collapseButton->setButtonText(" " + sectionName);
    // Bold the section headers
    collapseButton->setStyleSheet(collapseButton->styleSheet().append("font-weight: bold;"));
}

void
VMManagerDetailSection::addSection(const QString &name, const QString &value, VMManager::Display::Name displayField)
{
    const auto new_section = DetailSection { name, value };
    sections.push_back(new_section);
}

void
VMManagerDetailSection::setupMainLayout()
{
    // clang-tidy says I don't need to check before deleting
    delete mainLayout;
    mainLayout = new QVBoxLayout;
}
void
VMManagerDetailSection::setSections()
{
    int  row   = 0;
    bool empty = true;

    for (const auto &section : sections) {
        QStringList sectionsToAdd = section.value.split(sectionSeparator);
        QLabel     *labelKey      = nullptr;

        for (const auto &line : sectionsToAdd) {
            if (line.isEmpty()) {
                // Don't bother adding entries if the values are blank
                continue;
            }

            const auto labelValue = new QLabel();
            labelValue->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
            labelValue->setTextInteractionFlags(labelValue->textInteractionFlags() | Qt::TextSelectableByMouse);
            labelValue->setText(line);
            frameGridLayout->addWidget(labelValue, row, 1, Qt::AlignLeft);

            if (!labelKey) {
                labelKey = new QLabel();
                labelKey->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
                labelKey->setTextInteractionFlags(labelValue->textInteractionFlags());
                labelKey->setText(QCoreApplication::translate("", QString(section.name + ":").toUtf8().data()));
                frameGridLayout->addWidget(labelKey, row, 0, Qt::AlignLeft);
            }

            const auto hSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
            frameGridLayout->addItem(hSpacer, row, 2);
            empty = false;
            row++;
        }
    }

    collapseButton->setContent(ui->detailFrame);
    if (!empty)
        setVisible(true);
}
void
VMManagerDetailSection::clear()
{
    sections.clear();
    setVisible(false);

    // Clear everything out
    if (frameGridLayout) {
        while (frameGridLayout->count()) {
            QLayoutItem *cur_item = frameGridLayout->takeAt(0);
            if (cur_item->widget())
                delete cur_item->widget();
            delete cur_item;
        }
    }

    delete frameGridLayout;
    frameGridLayout = new QGridLayout();
    frameGridLayout->setContentsMargins(getMargins(MarginSection::DisplayGrid));
    ui->detailFrame->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->detailFrame->setLayout(frameGridLayout);
}

#ifdef Q_OS_WINDOWS
void
VMManagerDetailSection::updateStyle()
{
    const bool lightMode = util::isWindowsLightTheme();
    if (lightMode) {
        ui->outerFrame->setStyleSheet("");
        ui->collapseButtonHolder->setStyleSheet(HEADER_STYLESHEET_LIGHT);
    } else {
        ui->outerFrame->setStyleSheet(BACKGROUND_STYLESHEET_DARK);
        ui->collapseButtonHolder->setStyleSheet(HEADER_STYLESHEET_DARK);
    }
}
#endif

// QT for Linux and Windows doesn't have the same default margins as QT on MacOS.
// For consistency in appearance we'll have to return the margins on a per-OS basis
QMargins
VMManagerDetailSection::getMargins(const MarginSection section)
{
    switch (section) {
        case MarginSection::ToolButton:
#if defined(Q_OS_WINDOWS) or defined(Q_OS_LINUX)
            return { 10, 0, 5, 0 };
#else
            return { 0, 0, 5, 0 };
#endif
        case MarginSection::DisplayGrid:
#if defined(Q_OS_WINDOWS) or defined(Q_OS_LINUX)
            return { 10, 0, 0, 10 };
#else
            return { 0, 0, 0, 10 };
#endif
        default:
            return {};
    }
}

// CollapseButton Class

CollapseButton::CollapseButton(QWidget *parent)
    : QToolButton(parent)
    , content_(nullptr)
{
    setCheckable(true);
    setStyleSheet("background:none; border:none;");
    setIconSize(QSize(8, 8));
    setFont(QApplication::font());
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(this, &QToolButton::toggled, [=](const bool checked) {
        setArrowType(checked ? Qt::ArrowType::DownArrow : Qt::ArrowType::RightArrow);
        content_ != nullptr &&checked ? showContent() : hideContent();
    });
    setChecked(true);
}

void
CollapseButton::setButtonText(const QString &text)
{
    setText(" " + text);
}

void
CollapseButton::setContent(QWidget *content)
{
    assert(content != nullptr);
    content_              = content;
    const auto animation_ = new QPropertyAnimation(content_, "maximumHeight"); // QObject with auto delete
    animation_->setStartValue(0);
    animation_->setEasingCurve(QEasingCurve::InOutQuad);
    animation_->setDuration(300);
    animation_->setEndValue(content->geometry().height() + 50);
    // qDebug() << "section" << text() << "has a height of" << content->geometry().height();
    animator_.clear();
    animator_.addAnimation(animation_);
    if (!isChecked()) {
        content->setMaximumHeight(0);
    }
}

void
CollapseButton::hideContent()
{
    animator_.setDirection(QAbstractAnimation::Backward);
    animator_.start();
}

void
CollapseButton::showContent()
{
    animator_.setDirection(QAbstractAnimation::Forward);
    animator_.start();
}
