#ifndef QT_PROGSETTINGS_HPP
#define QT_PROGSETTINGS_HPP

#include <QDialog>

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

protected slots:
    void accept() override;
private slots:
    void on_pushButton_released();

private:
    Ui::ProgSettings *ui;

    friend class MainWindow;
};

#endif // QT_PROGSETTINGS_HPP
