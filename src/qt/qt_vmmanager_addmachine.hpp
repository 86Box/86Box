/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager add machine wizard
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_ADDMACHINE_H
#define QT_VMMANAGER_ADDMACHINE_H

#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QRegularExpression>
#include <QWizard>

// Implementation note: There are several classes in this header:
// One for the main Wizard class and one for each page of the wizard

class VMManagerAddMachine final : public QWizard {
    Q_OBJECT

public:
    enum {
        Page_Intro,
        Page_Fresh,
        Page_WithExistingConfig,
        Page_NameAndLocation,
        Page_Conclusion
    };

    explicit VMManagerAddMachine(QWidget *parent = nullptr);
};

class IntroPage : public QWizardPage {
    Q_OBJECT

public:
    explicit IntroPage(QWidget *parent = nullptr);
    [[nodiscard]] int nextId() const override;

private:
    QLabel       *topLabel;
    QRadioButton *newConfigRadioButton;
    QRadioButton *existingConfigRadioButton;
};

class WithExistingConfigPage final : public QWizardPage {
    Q_OBJECT
    Q_PROPERTY(QString configuration READ configuration WRITE setConfiguration NOTIFY configurationChanged)

public:
    explicit WithExistingConfigPage(QWidget *parent = nullptr);
    // These extra functions are required to register QPlainTextEdit fields
    [[nodiscard]] QString configuration() const;
    void                  setConfiguration(const QString &configuration);
signals:
    void configurationChanged(const QString &configuration);

private:
    QPlainTextEdit *existingConfiguration;
private slots:
    void chooseExistingConfigFile();

protected:
    [[nodiscard]] int  nextId() const override;
    [[nodiscard]] bool isComplete() const override;
};

class NameAndLocationPage final : public QWizardPage {
    Q_OBJECT

public:
    explicit NameAndLocationPage(QWidget *parent = nullptr);
    [[nodiscard]] int nextId() const override;

private:
    QLineEdit *systemName;
#ifdef CUSTOM_SYSTEM_LOCATION
    QLineEdit *systemLocation;
#endif
    QLineEdit *displayName;
    QLabel    *systemNameValidation;
#ifdef CUSTOM_SYSTEM_LOCATION
    QLabel            *systemLocationValidation;
    QRegularExpression dirValidate;
private slots:
    void chooseDirectoryLocation();
#endif
protected:
    [[nodiscard]] bool isComplete() const override;
    bool               eventFilter(QObject *watched, QEvent *event) override;
};

class ConclusionPage final : public QWizardPage {
    Q_OBJECT
public:
    explicit ConclusionPage(QWidget *parent = nullptr);

private:
    QLabel *topLabel;
    QLabel *systemName;
#ifdef CUSTOM_SYSTEM_LOCATION
    QLabel *systemLocation;
#endif
    QLabel *displayNameLabel;
    QLabel *displayName;

protected:
    void initializePage() override;
};

#endif // QT_VMMANAGER_ADDMACHINE_H
