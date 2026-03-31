#ifndef QT_PREFERENCES_HPP
#define QT_PREFERENCES_HPP

#include <QDialog>
#include <QTranslator>

namespace Ui {
class Preferences;
}

class PreferencesEmulator;
class PreferencesInput;
class PreferencesKeyBindings;

class Preferences : public QDialog {
    Q_OBJECT

public:
    explicit Preferences(QWidget *parent = nullptr);
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
    static CustomTranslator                *translator;
    static QTranslator                     *qtTranslator;
    static QVector<QPair<QString, QString>> languages;
    static QMap<int, std::wstring>          translatedstrings;
protected slots:
    void accept() override;

private:
    Ui::Preferences            *ui;
    static bool       loadQtTranslations(const QString name);

    PreferencesEmulator        *emulator;
    PreferencesInput           *input;
    PreferencesKeyBindings     *key_bindings;
};

#endif // QT_PREFERENCES_HPP
