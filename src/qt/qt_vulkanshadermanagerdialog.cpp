#include "qt_vulkanshadermanagerdialog.hpp"
#if QT_CONFIG(vulkan)
#include "ui_qt_vulkanshadermanagerdialog.h"

#include "qt_mainwindow.hpp"
#include "qt_util.hpp"
extern MainWindow *main_window;

#include "qt_vulkanshaderconfig.hpp"
#include "qt_slangp.hpp"

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

extern char vk_shader_file[20][512];
}

extern MainWindow *main_window;
#ifdef LIBRA_RUNTIME_VULKAN
slang_shader* slangp_parse(const char* path)
{
#ifndef LIBRASHADER_STATIC
    auto inst_res = ensure_librashader_instance();
    if (!inst_res) {
        return nullptr;
    }
#endif
    auto shader = new slang_shader{};
    shader->path = path;
#ifndef LIBRASHADER_STATIC
    auto err = librashader_inst.preset_create(path, &shader->shader_preset);
#else
    auto err = libra_preset_create(path, &shader->shader_preset);
#endif
    if (!shader->shader_preset) {
        char *errmsg = nullptr;
#ifndef LIBRASHADER_STATIC
        librashader_inst.error_write(err, &errmsg);
        QMessageBox::critical(main_window, QObject::tr("Error"), QString::fromUtf8(path) + "\n\n" + errmsg);
        librashader_inst.error_free_string(&errmsg);
        librashader_inst.error_free(&err);
#else
        libra_error_write(err, &errmsg);
        QMessageBox::critical(main_window, QObject::tr("Error"), QString::fromUtf8(path) + "\n\n" + errmsg);
        libra_error_free_string(&errmsg);
        libra_error_free(&err);
#endif
        delete shader;
        return nullptr;
    }
#ifndef LIBRASHADER_STATIC
    librashader_inst.preset_get_runtime_params(&shader->shader_preset, &shader->param_list);
#else
    libra_preset_get_runtime_params(&shader->shader_preset, &shader->param_list);
#endif
    slangp_read_shader_config(*shader);
    return shader;
}

void slangp_free(slang_shader* shader) {
#ifndef LIBRASHADER_STATIC
    librashader_inst.preset_free_runtime_params(shader->param_list);
    librashader_inst.preset_free(&shader->shader_preset);
#else
    libra_preset_free_runtime_params(shader->param_list);
    libra_preset_free(&shader->shader_preset);
#endif
    delete shader;
}
#endif

VulkanShaderManagerDialog::VulkanShaderManagerDialog(QWidget *parent, std::vector<std::string> device_names)
    : QDialog(parent)
    , ui(new Ui::VulkanShaderManagerDialog)
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

    for (auto& deviceName : device_names) {
        ui->comboBoxGPU->addItem(deviceName.c_str());
    }
    ui->comboBoxGPU->setCurrentIndex(std::clamp((uint32_t)video_vk_device, 0u, (uint32_t)device_names.size() - 1u));
    ui->comboBoxGPU->setEnabled(device_names.size() != 0);

#ifdef LIBRA_RUNTIME_VULKAN
#ifndef LIBRASHADER_STATIC
    if (!ensure_librashader_instance()) {
        ui->groupBoxShaders->hide();
        return;
    }
#endif

    for (int i = 0; i < 20; i++) {
        if (vk_shader_file[i][0] != 0) {
            char *filename = path_get_filename(vk_shader_file[i]);
            if (filename[0] != 0) {
                slang_shader *shaderfile = slangp_parse(vk_shader_file[i]);
                if (shaderfile) {
                    QListWidgetItem *item = new QListWidgetItem(ui->shaderListWidget);
                    item->setText(filename);
                    item->setData(Qt::UserRole + 1, QString(vk_shader_file[i]));
                    item->setData(Qt::UserRole + 2, (qulonglong) (uintptr_t) shaderfile);
                }
            }
        }
    }
    if (ui->shaderListWidget->count()) {
        ui->shaderListWidget->setCurrentRow(ui->shaderListWidget->count() - 1);
        auto current = ui->shaderListWidget->currentItem();
        if (current) {
            slang_shader *shader = (slang_shader *) current->data(Qt::UserRole + 2).toULongLong();
            if (shader->param_list.length > 0)
                ui->buttonConfigure->setEnabled(true);
            else
                ui->buttonConfigure->setEnabled(false);
        } else {
            ui->buttonConfigure->setEnabled(false);
        }
        ui->buttonAdd->setDisabled(ui->shaderListWidget->count() >= 20);
    } else {
        ui->buttonRemove->setDisabled(true);
        ui->buttonMoveUp->setDisabled(true);
        ui->buttonMoveDown->setDisabled(true);
        ui->buttonConfigure->setDisabled(true);
    }
#else
    ui->groupBoxShaders->hide();
#endif
}

VulkanShaderManagerDialog::~VulkanShaderManagerDialog()
{
#ifdef LIBRA_RUNTIME_VULKAN
    for (int i = 0; i < ui->shaderListWidget->count(); i++) {
        if (ui->shaderListWidget->item(i) && ui->shaderListWidget->item(i)->data(Qt::UserRole + 2).toULongLong()) {
            slang_shader* shader = (slang_shader*)ui->shaderListWidget->item(i)->data(Qt::UserRole + 2).toULongLong();
            slangp_free(shader);
        }
    }
#endif
    delete ui;
}

void
VulkanShaderManagerDialog::on_buttonBox_clicked(QAbstractButton *button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole) {
        accept();
    } else if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::RejectRole) {
        reject();
    } else if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole) {
        on_VulkanShaderManagerDialog_accepted();
        main_window->reloadAllRenderers();
    }
}

void
VulkanShaderManagerDialog::on_buttonMoveUp_clicked()
{
    if (ui->shaderListWidget->currentRow() == 0)
        return;

    int  row  = ui->shaderListWidget->currentRow();
    auto item = ui->shaderListWidget->takeItem(row);
    ui->shaderListWidget->insertItem(row - 1, item);
    ui->shaderListWidget->setCurrentItem(item);
}

void
VulkanShaderManagerDialog::on_shaderListWidget_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
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
            slang_shader *shader = (slang_shader *) current->data(Qt::UserRole + 2).toULongLong();
            if (shader->param_list.length > 0)
                ui->buttonConfigure->setEnabled(true);
        }
    }
    ui->buttonMoveUp->setDisabled(ui->shaderListWidget->currentRow() == 0);
    ui->buttonMoveDown->setDisabled(ui->shaderListWidget->currentRow() == (ui->shaderListWidget->count() - 1));
}

void
VulkanShaderManagerDialog::on_shaderListWidget_currentRowChanged(int currentRow)
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
            slang_shader *shader = (slang_shader *) current->data(Qt::UserRole + 2).toULongLong();
            if (shader->param_list.length > 0)
                ui->buttonConfigure->setEnabled(true);
        }
    }
    ui->buttonMoveUp->setDisabled(ui->shaderListWidget->currentRow() == 0);
    ui->buttonMoveDown->setDisabled(ui->shaderListWidget->currentRow() == (ui->shaderListWidget->count() - 1));
}

void
VulkanShaderManagerDialog::on_buttonMoveDown_clicked()
{
    if (ui->shaderListWidget->currentRow() == (ui->shaderListWidget->count() - 1))
        return;

    int  row  = ui->shaderListWidget->currentRow();
    auto item = ui->shaderListWidget->takeItem(row);
    ui->shaderListWidget->insertItem(row + 1, item);
    ui->shaderListWidget->setCurrentItem(item);
}

void
VulkanShaderManagerDialog::on_buttonAdd_clicked()
{
#ifdef LIBRA_RUNTIME_VULKAN
    auto res = QFileDialog::getOpenFileName(this, QString(), QString(),
                                            tr("Slang shaders") % util::DlgFilter({ "slangp" }) % tr("All files") % util::DlgFilter({ "*" }, true));
    if (!res.isEmpty()) {
        auto     glslp_file = res.toUtf8();
        slang_shader *shaderfile = slangp_parse(glslp_file.data());
        if (shaderfile) {
            auto             filename = path_get_filename(glslp_file.data());
            QListWidgetItem *item     = new QListWidgetItem(ui->shaderListWidget);
            item->setText(filename);
            item->setData(Qt::UserRole + 1, res);
            item->setData(Qt::UserRole + 2, (qulonglong) (uintptr_t) shaderfile);
            if (ui->shaderListWidget->count()) {
                ui->shaderListWidget->setCurrentRow(ui->shaderListWidget->count() - 1);
                ui->buttonAdd->setDisabled(ui->shaderListWidget->count() >= 20);
            }
        } else {
            QMessageBox::critical(this, tr("Slang error"), tr("Could not load file %1").arg(res));
        }
    }
#endif
}

void
VulkanShaderManagerDialog::on_buttonRemove_clicked()
{
#ifdef LIBRA_RUNTIME_VULKAN
    if (ui->shaderListWidget->currentItem()) {
        auto item = ui->shaderListWidget->takeItem(ui->shaderListWidget->currentRow());

        if (item->data(Qt::UserRole + 2).toULongLong()) {
            slangp_free((slang_shader *) item->data(Qt::UserRole + 2).toULongLong());
        }
        delete item;

        on_shaderListWidget_currentRowChanged(ui->shaderListWidget->currentRow());
    }
    ui->buttonAdd->setDisabled(ui->shaderListWidget->count() >= 20);
#endif
}

void
VulkanShaderManagerDialog::on_VulkanShaderManagerDialog_accepted()
{
#ifdef LIBRA_RUNTIME_VULKAN
    if (!ui->groupBoxShaders->isHidden()) {
        memset(vk_shader_file, 0, sizeof(vk_shader_file));
        for (int i = 0; i < ui->shaderListWidget->count(); i++) {
            strncpy(vk_shader_file[i], ui->shaderListWidget->item(i)->data(Qt::UserRole + 1).toString().toUtf8(), 512);
        }
    }
#endif
    startblit();
    video_vk_device = ui->comboBoxGPU->currentIndex();
    if (video_vk_device == -1)
        video_vk_device = 0;
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
VulkanShaderManagerDialog::on_buttonConfigure_clicked()
{
#ifdef LIBRA_RUNTIME_VULKAN
    auto item = ui->shaderListWidget->currentItem();
    if (item) {
        slang_shader *shader = (slang_shader *) item->data(Qt::UserRole + 2).toULongLong();

        auto configDialog = new VulkanShaderConfig(this, shader);
        configDialog->exec();
    }
#endif
}

void
VulkanShaderManagerDialog::on_radioButtonVideoSync_clicked()
{
    ui->targetFrameRate->setDisabled(true);
}

void
VulkanShaderManagerDialog::on_radioButtonTargetFramerate_clicked()
{
    ui->targetFrameRate->setDisabled(false);
}

void
VulkanShaderManagerDialog::on_horizontalSliderFramerate_sliderMoved(int position)
{
    (void) position;

    if (ui->horizontalSliderFramerate->value() != ui->targetFrameRate->value())
        ui->targetFrameRate->setValue(ui->horizontalSliderFramerate->value());
}

void
VulkanShaderManagerDialog::on_targetFrameRate_valueChanged(int arg1)
{
    (void) arg1;

    if (ui->horizontalSliderFramerate->value() != ui->targetFrameRate->value())
        ui->horizontalSliderFramerate->setValue(ui->targetFrameRate->value());
}
#endif
