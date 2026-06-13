#ifndef QT_VULKANSHADERMANAGERDIALOG_H
#define QT_VULKANSHADERMANAGERDIALOG_H

#include <QDialog>
#include <QAbstractButton>
#include <QListWidgetItem>

namespace Ui {
class VulkanShaderManagerDialog;
}

class VulkanShaderManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit VulkanShaderManagerDialog(QWidget *parent = nullptr);
    ~VulkanShaderManagerDialog();

private slots:
    void on_buttonBox_clicked(QAbstractButton *button);

    void on_buttonMoveUp_clicked();

    void on_shaderListWidget_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);

    void on_shaderListWidget_currentRowChanged(int currentRow);

    void on_buttonMoveDown_clicked();

    void on_buttonAdd_clicked();

    void on_buttonRemove_clicked();

    void on_VulkanShaderManagerDialog_accepted();

    void on_buttonConfigure_clicked();

    void on_radioButtonVideoSync_clicked();

    void on_radioButtonTargetFramerate_clicked();

    void on_horizontalSliderFramerate_sliderMoved(int position);

    void on_targetFrameRate_valueChanged(int arg1);

private:
    Ui::VulkanShaderManagerDialog *ui;
};

#endif // QT_VULKANSHADERMANAGERDIALOG_H
