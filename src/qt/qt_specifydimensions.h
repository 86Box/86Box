#ifndef QT_SPECIFYDIMENSIONS_H
#define QT_SPECIFYDIMENSIONS_H

#include <QDialog>

namespace Ui {
class SpecifyDimensions;
}

class SpecifyDimensions : public QDialog
{
    Q_OBJECT

public:
    explicit SpecifyDimensions(QWidget *parent = nullptr);
    ~SpecifyDimensions();

private slots:
    void on_SpecifyDimensions_accepted();

private:
    Ui::SpecifyDimensions *ui;
};

#endif // QT_SPECIFYDIMENSIONS_H
