#ifndef QT_CGASETTINGSDIALOG_HPP
#define QT_CGASETTINGSDIALOG_HPP

#include <QDialog>

namespace Ui {
class CGASettingsDialog;
}

class CGASettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit CGASettingsDialog(QWidget *parent = nullptr);
    ~CGASettingsDialog();

private slots:
    void on_buttonBox_accepted();

    void on_buttonBox_rejected();

private:
    Ui::CGASettingsDialog *ui;

    void applySettings();
    void updateDisplay();

    int cga_hue, cga_saturation, cga_sharpness, cga_brightness, cga_contrast;
};

#endif // QT_CGASETTINGSDIALOG_HPP
