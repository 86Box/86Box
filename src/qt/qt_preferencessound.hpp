#ifndef QT_PREFERENCESSOUND_HPP
#define QT_PREFERENCESSOUND_HPP

#include <QWidget>

namespace Ui {
class PreferencesSound;
}

/* The audio backend and output device, shared by the emulator and the
   VM manager. */
class PreferencesSound : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesSound(QWidget *parent = nullptr);
    ~PreferencesSound();

    void save();

private slots:
    void on_comboBoxSoundBackend_currentIndexChanged(int index);

private:
    void populateAudioOutputDevices(int backend);

    Ui::PreferencesSound *ui;

    bool populatingBackend = false;
};

#endif // QT_PREFERENCESSOUND_HPP
