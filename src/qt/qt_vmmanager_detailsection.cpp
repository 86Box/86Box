/*
* 86Box	A hypervisor and IBM PC system emulator that specializes in
*		running old operating systems and software designed for IBM
*		PC systems and compatibles from 1981 through fairly recent
*		system designs based on the PCI bus.
*
*		This file is part of the 86Box distribution.
*
*		86Box VM manager system details section module
*
*
*
* Authors:	cold-brewed
*
*		Copyright 2024 cold-brewed
*/

#include "qt_vmmanager_detailsection.hpp"
#include "ui_qt_vmmanager_detailsection.h"

#include <QPushButton>

const QString VMManagerDetailSection::sectionSeparator = ";";

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
    const bool lightMode = QApplication::palette().window().color().value() > QApplication::palette().windowText().color().value();
    // Alternate layout
    if ( lightMode) {
        ui->collapseButtonHolder->setStyleSheet("background-color: palette(midlight);");
    } else {
        ui->collapseButtonHolder->setStyleSheet("background-color: palette(mid);");
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
}

VMManagerDetailSection::
VMManagerDetailSection(const QVariant &varSectionName) : ui(new Ui::DetailSection)
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

    mainLayout      = new QVBoxLayout();
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
VMManagerDetailSection::addSection(const QString &name, const QString &value, Display::Name displayField)
{
    const auto new_section = DetailSection { name, value};
    sections.push_back(new_section);
    setSections();
}

void
VMManagerDetailSection::setupMainLayout()
{
    // clang-tidy says I don't need to check before deleting
    delete mainLayout;
    mainLayout = new QVBoxLayout;
}
void
VMManagerDetailSection::clearContentsSetupGrid()
{
    // Clear everything out
    if(frameGridLayout) {
        while(frameGridLayout->count()) {
            QLayoutItem * cur_item = frameGridLayout->takeAt(0);
            if(cur_item->widget())
                delete cur_item->widget();
            delete cur_item;
        }
    }

    delete frameGridLayout;
    frameGridLayout = new QGridLayout();
    qint32 *left = nullptr, *top = nullptr, *right = nullptr, *bottom = nullptr;
    frameGridLayout->getContentsMargins(left, top, right, bottom);
    frameGridLayout->setContentsMargins(getMargins(MarginSection::DisplayGrid));
    ui->detailFrame->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->detailFrame->setLayout(frameGridLayout);
}
void
VMManagerDetailSection::setSections()
{
    clearContentsSetupGrid();
    int row = 0;


    for ( const auto& section : sections) {
        // if the string contains the separator (defined elsewhere) then split and
        // add each entry on a new line. Otherwise, just add the one.
        QStringList sectionsToAdd;
        if(section.value.contains(sectionSeparator)) {
            sectionsToAdd = section.value.split(sectionSeparator);
        } else {
            sectionsToAdd.push_back(section.value);
        }
        bool keyAdded = false;
        for(const auto&line : sectionsToAdd) {
            if(line.isEmpty()) {
                // Don't bother adding entries if the values are blank
                continue;
            }
            const auto labelKey = new QLabel();
            labelKey->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            const auto labelValue = new QLabel();
            labelKey->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
            labelValue->setTextInteractionFlags(labelValue->textInteractionFlags() | Qt::TextSelectableByMouse);
            labelKey->setTextInteractionFlags(labelValue->textInteractionFlags() | Qt::TextSelectableByMouse);

            // Reduce the text size for the label
            // First, get the existing font
            auto smaller_font = labelValue->font();
            // Get a smaller size
            // Not sure if I like the smaller size, back to regular for now
            // auto smaller_size = 0.85 * smaller_font.pointSize();
            const auto smaller_size = 1 * smaller_font.pointSize();
            // Set the font to the smaller size
            smaller_font.setPointSizeF(smaller_size);
            // Assign that new, smaller font to the label
            labelKey->setFont(smaller_font);
            labelValue->setFont(smaller_font);

            labelKey->setText(section.name + ":");
            labelValue->setText(line);
            if(!keyAdded) {
                frameGridLayout->addWidget(labelKey, row, 0, Qt::AlignLeft);
                keyAdded = true;
            }
            frameGridLayout->addWidget(labelValue, row, 1, Qt::AlignLeft);
            const auto hSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
            frameGridLayout->addItem(hSpacer, row, 2);
            row++;
        }
    }
    collapseButton->setContent(ui->detailFrame);
}
void
VMManagerDetailSection::clear()
{
    sections.clear();
}

// QT for Linux and Windows doesn't have the same default margins as QT on MacOS.
// For consistency in appearance we'll have to return the margins on a per-OS basis
QMargins
VMManagerDetailSection::getMargins(const MarginSection section)
{
    switch (section) {
        case MarginSection::ToolButton:
#if defined(Q_OS_WINDOWS) or defined(Q_OS_LINUX)
            return {10, 0, 5, 0};
#else
            return {0, 0, 5, 0};
#endif
        case MarginSection::DisplayGrid:
#if defined(Q_OS_WINDOWS) or defined(Q_OS_LINUX)
            return {10, 0, 0, 10};
#else
            return {0, 0, 0, 10};
#endif
        default:
            return {};
    }
}

// CollapseButton Class

CollapseButton::CollapseButton(QWidget *parent) : QToolButton(parent), content_(nullptr) {
    setCheckable(true);
    setStyleSheet("background:none; border:none;");
    setIconSize(QSize(8, 8));
    setFont(QApplication::font());
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(this, &QToolButton::toggled, [=](const bool checked) {
        setArrowType(checked ? Qt::ArrowType::DownArrow : Qt::ArrowType::RightArrow);
        content_ != nullptr && checked ? showContent() : hideContent();
    });
    setChecked(true);
}

void CollapseButton::setButtonText(const QString &text) {
    setText(" " + text);
}

void CollapseButton::setContent(QWidget *content) {
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

void CollapseButton::hideContent() {
    animator_.setDirection(QAbstractAnimation::Backward);
    animator_.start();
}

void CollapseButton::showContent() {
    animator_.setDirection(QAbstractAnimation::Forward);
    animator_.start();
}

