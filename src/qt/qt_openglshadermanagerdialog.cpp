#include "qt_openglshadermanagerdialog.hpp"
#include "ui_qt_openglshadermanagerdialog.h"

#include "qt_mainwindow.hpp"
#include "qt_util.hpp"
extern MainWindow *main_window;

#include "qt_openglshaderconfig.hpp"

#include <QListWidgetItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QStringBuilder>

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/path.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/qt-glslp-parser.h>

extern char gl3_shader_file[MAX_USER_SHADERS][512];
}

OpenGLShaderManagerDialog::OpenGLShaderManagerDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OpenGLShaderManagerDialog)
{
    ui->setupUi(this);

    ui->checkBoxVSync->setChecked(!!video_vsync);
    ui->radioButtonVideoSync->setChecked(video_framerate == -1);
    ui->radioButtonTargetFramerate->setChecked(video_framerate != -1);
    if (video_framerate != -1) {
        ui->targetFrameRate->setValue(video_framerate);
    } else {
        ui->targetFrameRate->setDisabled(true);
    }

    for (int i = 0; i < MAX_USER_SHADERS; i++) {
        if (gl3_shader_file[i][0] != 0) {
            char *filename = path_get_filename(gl3_shader_file[i]);
            if (filename[0] != 0) {
                glslp_t *shaderfile = glslp_parse(gl3_shader_file[i]);
                if (shaderfile) {
                    QListWidgetItem *item = new QListWidgetItem(ui->shaderListWidget);
                    item->setText(filename);
                    item->setData(Qt::UserRole + 1, QString(gl3_shader_file[i]));
                    item->setData(Qt::UserRole + 2, (qulonglong) (uintptr_t) shaderfile);
                }
            }
        }
    }
    if (ui->shaderListWidget->count()) {
        ui->shaderListWidget->setCurrentRow(ui->shaderListWidget->count() - 1);
        auto current = ui->shaderListWidget->currentItem();
        if (current) {
            glslp_t *shader = (glslp_t *) current->data(Qt::UserRole + 2).toULongLong();
            if (shader->num_parameters > 0)
                ui->buttonConfigure->setEnabled(true);
            else
                ui->buttonConfigure->setEnabled(false);
        } else {
            ui->buttonConfigure->setEnabled(false);
        }
        ui->buttonAdd->setDisabled(ui->shaderListWidget->count() >= MAX_USER_SHADERS);
    } else {
        ui->buttonRemove->setDisabled(true);
        ui->buttonMoveUp->setDisabled(true);
        ui->buttonMoveDown->setDisabled(true);
        ui->buttonConfigure->setDisabled(true);
    }
}

OpenGLShaderManagerDialog::~OpenGLShaderManagerDialog()
{
    for (int i = 0; i < ui->shaderListWidget->count(); i++) {
        if (ui->shaderListWidget->item(i) && ui->shaderListWidget->item(i)->data(Qt::UserRole + 2).toULongLong()) {
            glslp_free((glslp_t *) ui->shaderListWidget->item(i)->data(Qt::UserRole + 2).toULongLong());
        }
    }
    delete ui;
}

void
OpenGLShaderManagerDialog::on_buttonBox_clicked(QAbstractButton *button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole) {
        accept();
    } else if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::RejectRole) {
        reject();
    } else if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole) {
        on_OpenGLShaderManagerDialog_accepted();
        main_window->reloadAllRenderers();
    }
}

void
OpenGLShaderManagerDialog::on_buttonMoveUp_clicked()
{
    if (ui->shaderListWidget->currentRow() == 0)
        return;

    int  row  = ui->shaderListWidget->currentRow();
    auto item = ui->shaderListWidget->takeItem(row);
    ui->shaderListWidget->insertItem(row - 1, item);
    ui->shaderListWidget->setCurrentItem(item);
}

void
OpenGLShaderManagerDialog::on_shaderListWidget_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (current == nullptr) {
        ui->buttonRemove->setDisabled(true);
        ui->buttonMoveUp->setDisabled(true);
        ui->buttonMoveDown->setDisabled(true);
        ui->buttonConfigure->setDisabled(true);
        return;
    } else {
        ui->buttonRemove->setDisabled(false);
        ui->buttonConfigure->setDisabled(true);
        if (current) {
            glslp_t *shader = (glslp_t *) current->data(Qt::UserRole + 2).toULongLong();
            if (shader->num_parameters > 0)
                ui->buttonConfigure->setEnabled(true);
        }
    }
    ui->buttonMoveUp->setDisabled(ui->shaderListWidget->currentRow() == 0);
    ui->buttonMoveDown->setDisabled(ui->shaderListWidget->currentRow() == (ui->shaderListWidget->count() - 1));
}

void
OpenGLShaderManagerDialog::on_shaderListWidget_currentRowChanged(int currentRow)
{
    auto current = ui->shaderListWidget->currentItem();
    if (current == nullptr) {
        ui->buttonRemove->setDisabled(true);
        ui->buttonMoveUp->setDisabled(true);
        ui->buttonMoveDown->setDisabled(true);
        ui->buttonConfigure->setDisabled(true);
        return;
    } else {
        ui->buttonRemove->setDisabled(false);
        ui->buttonConfigure->setDisabled(true);
        if (current) {
            glslp_t *shader = (glslp_t *) current->data(Qt::UserRole + 2).toULongLong();
            if (shader->num_parameters > 0)
                ui->buttonConfigure->setEnabled(true);
        }
    }
    ui->buttonMoveUp->setDisabled(ui->shaderListWidget->currentRow() == 0);
    ui->buttonMoveDown->setDisabled(ui->shaderListWidget->currentRow() == (ui->shaderListWidget->count() - 1));
}

void
OpenGLShaderManagerDialog::on_buttonMoveDown_clicked()
{
    if (ui->shaderListWidget->currentRow() == (ui->shaderListWidget->count() - 1))
        return;

    int  row  = ui->shaderListWidget->currentRow();
    auto item = ui->shaderListWidget->takeItem(row);
    ui->shaderListWidget->insertItem(row + 1, item);
    ui->shaderListWidget->setCurrentItem(item);
}

void
OpenGLShaderManagerDialog::on_buttonAdd_clicked()
{
    auto res = QFileDialog::getOpenFileName(this, QString(), QString(),
                                            tr("GLSL shaders") % util::DlgFilter({ "glslp", "glsl" }) % tr("All files") % util::DlgFilter({ "*" }, true));
    if (!res.isEmpty()) {
        auto     glslp_file = res.toUtf8();
        glslp_t *shaderfile = glslp_parse(glslp_file.data());
        if (shaderfile) {
            auto             filename = path_get_filename(glslp_file.data());
            QListWidgetItem *item     = new QListWidgetItem(ui->shaderListWidget);
            item->setText(filename);
            item->setData(Qt::UserRole + 1, res);
            item->setData(Qt::UserRole + 2, (qulonglong) (uintptr_t) shaderfile);
            if (ui->shaderListWidget->count()) {
                ui->shaderListWidget->setCurrentRow(ui->shaderListWidget->count() - 1);
                ui->buttonAdd->setDisabled(ui->shaderListWidget->count() >= MAX_USER_SHADERS);
            }
        } else {
            QMessageBox::critical(this, tr("GLSL error"), tr("Could not load file %1").arg(res));
        }
    }
}

void
OpenGLShaderManagerDialog::on_buttonRemove_clicked()
{
    if (ui->shaderListWidget->currentItem()) {
        auto item = ui->shaderListWidget->takeItem(ui->shaderListWidget->currentRow());

        if (item->data(Qt::UserRole + 2).toULongLong()) {
            glslp_free((glslp_t *) item->data(Qt::UserRole + 2).toULongLong());
        }
        delete item;

        on_shaderListWidget_currentRowChanged(ui->shaderListWidget->currentRow());
    }
    ui->buttonAdd->setDisabled(ui->shaderListWidget->count() >= MAX_USER_SHADERS);
}

void
OpenGLShaderManagerDialog::on_OpenGLShaderManagerDialog_accepted()
{
    memset(gl3_shader_file, 0, sizeof(gl3_shader_file));
    for (int i = 0; i < ui->shaderListWidget->count(); i++) {
        strncpy(gl3_shader_file[i], ui->shaderListWidget->item(i)->data(Qt::UserRole + 1).toString().toUtf8(), 512);
    }
    startblit();
    video_vsync = ui->checkBoxVSync->isChecked();
    if (ui->radioButtonTargetFramerate->isChecked()) {
        video_framerate = ui->horizontalSliderFramerate->value();
    } else {
        video_framerate = -1;
    }
    config_save();
    endblit();
}

void
OpenGLShaderManagerDialog::on_buttonConfigure_clicked()
{
    auto item = ui->shaderListWidget->currentItem();
    if (item) {
        glslp_t *shader = (glslp_t *) item->data(Qt::UserRole + 2).toULongLong();

        auto configDialog = new OpenGLShaderConfig(this, shader);
        configDialog->exec();
    }
}

void
OpenGLShaderManagerDialog::on_radioButtonVideoSync_clicked()
{
    ui->targetFrameRate->setDisabled(true);
}

void
OpenGLShaderManagerDialog::on_radioButtonTargetFramerate_clicked()
{
    ui->targetFrameRate->setDisabled(false);
}

void
OpenGLShaderManagerDialog::on_horizontalSliderFramerate_sliderMoved(int position)
{
    (void) position;

    if (ui->horizontalSliderFramerate->value() != ui->targetFrameRate->value())
        ui->targetFrameRate->setValue(ui->horizontalSliderFramerate->value());
}

void
OpenGLShaderManagerDialog::on_targetFrameRate_valueChanged(int arg1)
{
    (void) arg1;

    if (ui->horizontalSliderFramerate->value() != ui->targetFrameRate->value())
        ui->horizontalSliderFramerate->setValue(ui->targetFrameRate->value());
}
