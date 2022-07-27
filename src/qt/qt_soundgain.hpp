#ifndef QT_SOUNDGAIN_HPP
#define QT_SOUNDGAIN_HPP

#include <QDialog>

namespace Ui {
class SoundGain;
}

class SoundGain : public QDialog
{
    Q_OBJECT

public:
    explicit SoundGain(QWidget *parent = nullptr);
    ~SoundGain();

private slots:
    void on_verticalSlider_valueChanged(int value);

    void on_SoundGain_rejected();

private:
    Ui::SoundGain *ui;
    int sound_gain_orig;
};

#endif // QT_SOUNDGAIN_HPP
