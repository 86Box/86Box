#ifndef QT_NEWFLASHDIALOG_HPP
#define QT_NEWFLASHDIALOG_HPP

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QSpinBox;

/* Creates the raw image of a new USB flash drive, sized in MB or GB and
   formatted FAT16 or FAT32, or left blank. */
class NewFlashDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewFlashDialog(QWidget *parent = nullptr);

    QString fileName() const { return fn; }

public slots:
    void accept() override;

private:
    QLineEdit        *lineEditFile;
    QComboBox        *comboBoxSize;
    QSpinBox         *spinBoxCustom;
    QComboBox        *comboBoxUnit;
    QComboBox        *comboBoxFormat;
    QDialogButtonBox *buttonBox;
    QString           fn;

    uint64_t sizeBytes() const;
    void     updateSize();
    void     updateOk();
};

#endif // QT_NEWFLASHDIALOG_HPP
