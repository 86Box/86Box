#include "qt_cgasettingsdialog.hpp"
#include "ui_qt_cgasettingsdialog.h"

#include <QPushButton>

extern "C"
{
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/vid_cga_comp.h>
}

CGASettingsDialog::CGASettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CGASettingsDialog)
{
    ui->setupUi(this);

    cga_hue = vid_cga_comp_hue;
    cga_saturation = vid_cga_comp_saturation;
    cga_brightness = vid_cga_comp_brightness;
    cga_contrast = vid_cga_comp_contrast;
    cga_sharpness = vid_cga_comp_sharpness;

    ui->horizontalSliderHue->setValue(vid_cga_comp_hue);
    ui->horizontalSliderSaturation->setValue(vid_cga_comp_saturation);
    ui->horizontalSliderBrightness->setValue(vid_cga_comp_brightness);
    ui->horizontalSliderContrast->setValue(vid_cga_comp_contrast);
    ui->horizontalSliderSharpness->setValue(vid_cga_comp_sharpness);

    connect(ui->buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &CGASettingsDialog::applySettings);
    connect(ui->buttonBox->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this]
    {
        ui->horizontalSliderHue->setValue(0);
        ui->horizontalSliderSaturation->setValue(100);
        ui->horizontalSliderBrightness->setValue(0);
        ui->horizontalSliderContrast->setValue(100);
        ui->horizontalSliderSharpness->setValue(0);
    });

    connect(ui->horizontalSliderHue, &QSlider::valueChanged, this, [this] { updateDisplay(); } );
    connect(ui->horizontalSliderSaturation, &QSlider::valueChanged, this, [this] { updateDisplay(); } );
    connect(ui->horizontalSliderBrightness, &QSlider::valueChanged, this, [this] { updateDisplay(); } );
    connect(ui->horizontalSliderContrast, &QSlider::valueChanged, this, [this] { updateDisplay(); } );
    connect(ui->horizontalSliderSharpness, &QSlider::valueChanged, this, [this] { updateDisplay(); } );
}

CGASettingsDialog::~CGASettingsDialog()
{
    delete ui;
}

void CGASettingsDialog::updateDisplay()
{
    auto temp_cga_comp_hue        = ui->horizontalSliderHue->value();
    auto temp_cga_comp_saturation = ui->horizontalSliderSaturation->value();
    auto temp_cga_comp_brightness = ui->horizontalSliderBrightness->value();
    auto temp_cga_comp_contrast   = ui->horizontalSliderContrast->value();
    auto temp_cga_comp_sharpness  = ui->horizontalSliderSharpness->value();
    cga_comp_reload(temp_cga_comp_brightness, temp_cga_comp_saturation, temp_cga_comp_sharpness, temp_cga_comp_hue, temp_cga_comp_contrast);
}

void CGASettingsDialog::applySettings()
{
    vid_cga_comp_hue        = ui->horizontalSliderHue->value();
    vid_cga_comp_saturation = ui->horizontalSliderSaturation->value();
    vid_cga_comp_brightness = ui->horizontalSliderBrightness->value();
    vid_cga_comp_contrast   = ui->horizontalSliderContrast->value();
    vid_cga_comp_sharpness  = ui->horizontalSliderSharpness->value();
    cga_comp_reload(vid_cga_comp_brightness, vid_cga_comp_saturation, vid_cga_comp_sharpness, vid_cga_comp_hue, vid_cga_comp_contrast);

    cga_hue = vid_cga_comp_hue;
    cga_saturation = vid_cga_comp_saturation;
    cga_brightness = vid_cga_comp_brightness;
    cga_contrast = vid_cga_comp_contrast;
    cga_sharpness = vid_cga_comp_sharpness;
}

void CGASettingsDialog::on_buttonBox_accepted()
{
    applySettings();
}

void CGASettingsDialog::on_buttonBox_rejected()
{
    vid_cga_comp_hue = cga_hue;
    vid_cga_comp_saturation = cga_saturation;
    vid_cga_comp_brightness = cga_brightness;
    vid_cga_comp_contrast = cga_contrast;
    vid_cga_comp_sharpness = cga_sharpness;

    cga_comp_reload(vid_cga_comp_brightness, vid_cga_comp_saturation, vid_cga_comp_sharpness, vid_cga_comp_hue, vid_cga_comp_contrast);
}

