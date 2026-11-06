#ifndef QT_PREFERENCES_HPP
#define QT_PREFERENCES_HPP

#include <QTranslator>

#include "qt_pageddialog.hpp"

class PreferencesGeneral;
class PreferencesEmulator;
class PreferencesInput;
class PreferencesKeyBindings;
class PreferencesManager;

/* The preferences of the emulator and the VM manager, saved to the global
   configuration. The pages only save the preferences; applying them is up
   to the caller. */
class Preferences : public PagedDialog {
    Q_OBJECT

public:
    /* The manager page is shown in the built-in VM manager, and in a machine
       it supervises; while machines are running, it cannot change the system
       directory. */
    Preferences(bool showManager, bool machinesRunning, QWidget *parent = nullptr);
    ~Preferences();
    void save();

    static Preferences *preferences;

    /* Move to qt_preferencesemulator.cpp? */
#ifdef Q_OS_WINDOWS
    static QFont getUIFont();
#endif
    static int     languageCodeToId(QString langCode);
    static QString languageIdToCode(int id);
    static void    getSysLang(QObject *parent = nullptr);
    static void    loadTranslators(QObject *parent = nullptr);
    static void    reloadStrings();
    class CustomTranslator : public QTranslator {
    public:
        CustomTranslator(QObject *parent = nullptr)
            : QTranslator(parent) {};

protected:
        QString translate(const char *context, const char *sourceText,
                          const char *disambiguation = nullptr, int n = -1) const override
        {
            return QTranslator::translate("", sourceText, disambiguation, n);
        }
    };
    static bool                             translationsLoaded;
    static CustomTranslator                *translator;
    static QTranslator                     *qtTranslator;
    static QVector<QPair<QString, QString>> languages;
    static QMap<int, QByteArray>            translatedstrings;
protected slots:
    void accept() override;

private:
    static bool       loadQtTranslations(const QString name);

    PreferencesGeneral         *general;
    PreferencesEmulator        *emulator;
    PreferencesInput           *input;
    PreferencesKeyBindings     *key_bindings;
    PreferencesManager         *manager = nullptr;
};

#endif // QT_PREFERENCES_HPP
