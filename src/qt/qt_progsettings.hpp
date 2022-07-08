#ifndef QT_PROGSETTINGS_HPP
#define QT_PROGSETTINGS_HPP

#include <QDialog>
#include <QTranslator>

namespace Ui {
class ProgSettings;
}

class ProgSettings : public QDialog
{
    Q_OBJECT

public:
    explicit ProgSettings(QWidget *parent = nullptr);
    ~ProgSettings();
    static QString getIconSetPath();
    static QIcon loadIcon(QString file);
    static void loadTranslators(QObject* parent = nullptr);
    static void reloadStrings();
    class CustomTranslator : public QTranslator
    {
    public:
        CustomTranslator(QObject* parent = nullptr) : QTranslator(parent) {};
    protected:
        QString translate(const char *context, const char *sourceText,
                                      const char *disambiguation = nullptr, int n = -1) const override
        {
            if (strcmp(sourceText, "&Fullscreen") == 0) sourceText = "&Fullscreen\tCtrl+Alt+PgUp";
            if (strcmp(sourceText, "&Ctrl+Alt+Del") == 0) sourceText = "&Ctrl+Alt+Del\tCtrl+F12";
            if (strcmp(sourceText, "Take s&creenshot") == 0) sourceText = "Take s&creenshot\tCtrl+F11";
            if (strcmp(sourceText, "Begin trace") == 0) sourceText = "Begin trace\tCtrl+T";
            if (strcmp(sourceText, "End trace") == 0) sourceText = "End trace\tCtrl+T";
            if (strcmp(sourceText, "&Qt (Software)") == 0)
            {
                QString finalstr = QTranslator::translate("", "&SDL (Software)", disambiguation, n);
                finalstr.replace("SDL", "Qt");
                finalstr.replace("(&S)", "(&Q)");
                return finalstr;
            }
            QString finalstr = QTranslator::translate("", sourceText, disambiguation, n);
#ifdef Q_OS_MACOS
            if (finalstr.contains('\t')) finalstr.truncate(finalstr.indexOf('\t'));
#endif
            return finalstr;
        }
    };
    static CustomTranslator* translator;
    static QTranslator* qtTranslator;
    static QMap<uint32_t, QPair<QString, QString>> lcid_langcode;
    static QMap<int, std::wstring> translatedstrings;

protected slots:
    void accept() override;
private slots:
    void on_pushButton_released();
    void on_pushButtonLanguage_released();

    void on_horizontalSlider_valueChanged(int value);

    void on_pushButton_2_clicked();

private:
    Ui::ProgSettings *ui;

    friend class MainWindow;
    double mouseSensitivity;
};

#endif // QT_PROGSETTINGS_HPP
