#ifndef QT_FILEFIELD_HPP
#define QT_FILEFIELD_HPP

#include <QWidget>

namespace Ui {
class FileField;
}

class FileField : public QWidget
{
    Q_OBJECT

public:
    explicit FileField(QWidget *parent = nullptr);
    ~FileField();

    QString fileName() const { return fileName_; }
    void setFileName(const QString& fileName);

    void setFilter(const QString& filter) { filter_ = filter; }
    QString selectedFilter() const { return selectedFilter_; }

    void setCreateFile(bool createFile) { createFile_ = createFile; }
    bool createFile() { return createFile_; }

signals:
    void fileSelected(const QString& fileName);

private slots:
    void on_pushButton_clicked();

private:
    Ui::FileField *ui;
    QString fileName_;
    QString selectedFilter_;
    QString filter_;
    bool createFile_ = false;
};

#endif // QT_FILEFIELD_HPP
