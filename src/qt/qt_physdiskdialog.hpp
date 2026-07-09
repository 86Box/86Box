#include <QDialog>
#include <QString>

namespace Ui {
class PhysicalHddDialog;
}

class PhysicalHddDialog : public QDialog
{
    Q_OBJECT
public:
    PhysicalHddDialog(QWidget* parent);
    ~PhysicalHddDialog();

private:
    Ui::PhysicalHddDialog *ui;
};
