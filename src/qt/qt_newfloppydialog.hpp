#ifndef QT_NEWFLOPPYDIALOG_HPP
#define QT_NEWFLOPPYDIALOG_HPP

#include <QDialog>

namespace Ui {
class NewFloppyDialog;
}

struct disk_size_t;
class QProgressDialog;

class NewFloppyDialog : public QDialog
{
    Q_OBJECT

public:
    enum class MediaType {
        Floppy,
        Zip,
        Mo,
    };
    enum class FileType {
        Img,
        Fdi,
        Zdi,
        Mdi,
    };
    explicit NewFloppyDialog(MediaType type, QWidget *parent = nullptr);
    ~NewFloppyDialog();

    QString fileName() const;

signals:
    void fileProgress(int i);

private slots:
    void onCreate();

private:
    Ui::NewFloppyDialog *ui;
    MediaType mediaType_;

    bool create86f(const QString& filename, const disk_size_t& disk_size, uint8_t rpm_mode);
    bool createSectorImage(const QString& filename, const disk_size_t& disk_size, FileType type);
    bool createZipSectorImage(const QString& filename, const disk_size_t& disk_size, FileType type, QProgressDialog& pbar);
    bool createMoSectorImage(const QString& filename, int8_t disk_size, FileType type, QProgressDialog& pbar);
};

#endif // QT_NEWFLOPPYDIALOG_HPP
