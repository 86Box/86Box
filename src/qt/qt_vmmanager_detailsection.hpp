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
#ifndef QT_VMMANAGER_DETAILSECTION_H
#define QT_VMMANAGER_DETAILSECTION_H

#include <QLabel>
#include <QToolButton>
#include <QGridLayout>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QApplication>
#include "qt_vmmanager_system.hpp"

QT_BEGIN_NAMESPACE
namespace Ui {
class DetailSection;
}
QT_END_NAMESPACE

class CollapseButton final : public QToolButton {
    Q_OBJECT

public:
    explicit CollapseButton(QWidget *parent = nullptr);

    void setButtonText(const QString &text);

    void setContent(QWidget *content);

    void hideContent();

    void showContent();

private:
    QWidget                *content_;
    QString                 text_;
    QParallelAnimationGroup animator_;
};

class VMManagerDetailSection final : public QWidget {
    Q_OBJECT

public:
    explicit VMManagerDetailSection(const QString &sectionName);
    explicit VMManagerDetailSection(const QVariant &varSectionName);
    // explicit VMManagerDetailSection();

    ~VMManagerDetailSection() override;

    void addSection(const QString &name, const QString &value, VMManager::Display::Name displayField = VMManager::Display::Name::Unknown);
    void setSections();
    void clear();

    QLabel         *tableLabel;
    CollapseButton *collapseButton;
    //    QGridLayout *buttonGridLayout;
    QGridLayout *frameGridLayout;
    QVBoxLayout *mainLayout;
    QHBoxLayout *buttonLayout;
    QFrame      *frame;

    static const QString sectionSeparator;

#ifdef Q_OS_WINDOWS
public slots:
    void updateStyle();
#endif

private:
    enum class MarginSection {
        ToolButton,
        DisplayGrid,
    };

    void setSectionName(const QString &name);
    void setupMainLayout();
    void clearContentsSetupGrid();

    static QMargins getMargins(MarginSection section);

    QString sectionName;

    struct DetailSection {
        QString name;
        QString value;
    };

    QVector<DetailSection> sections;
    Ui::DetailSection     *ui;
};

#endif // QT_VMMANAGER_DETAILSECTION_H
