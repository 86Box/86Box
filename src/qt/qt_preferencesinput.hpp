#ifndef QT_PREFERENCESINPUT_HPP
#define QT_PREFERENCESINPUT_HPP

#include <QWidget>
#include <QtGui/QStandardItemModel>
#include <QtGui/QStandardItem>
#include <QItemDelegate>
#include <QPainter>
#include <QVariant>
#include <QTableWidget>

namespace Ui {
class PreferencesInput;
}

class PreferencesInput : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesInput(QWidget *parent = nullptr);
    ~PreferencesInput();

    void save();

private slots:
    void on_horizontalSlider_valueChanged(int value);

    void on_pushButton_2_clicked();

private:
    Ui::PreferencesInput *ui;

    double mouseSensitivity;
};

#endif // QT_PREFERENCESINPUT_HPP
