#ifndef QT_PROGSETTINGS_HPP
#define QT_PROGSETTINGS_HPP

#include <QDialog>
#include <QTranslator>

namespace Ui {
class ProgSettings;
}

class ProgSettings : public QDialog {
    Q_OBJECT

public:
    explicit ProgSettings(QWidget *parent = nullptr);
    ~ProgSettings();
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
private slots:
    void on_pushButtonLanguage_released();

    void on_horizontalSlider_valueChanged(int value);

    void on_pushButton_2_clicked();

private:
    Ui::ProgSettings *ui;
    static bool       loadQtTranslations(const QString name);

    friend class MainWindow;
    double mouseSensitivity;
};

#endif // QT_PROGSETTINGS_HPP
