#ifndef QT_DIRFIELD_HPP
#define QT_DIRFIELD_HPP

#include <QWidget>

namespace Ui {
class DirField;
}

class DirField : public QWidget {
    Q_OBJECT

public:
    explicit DirField(QWidget *parent = nullptr);
    ~DirField();

    QString dirName() const { return dirName_; }
    void    setDirName(const QString &dirName);

signals:
    void dirSelected(const QString &dirName, bool precheck = false);
    void dirTextEntered(const QString &dirName, bool precheck = false);

private slots:
    void on_pushButton_clicked();

private:
    Ui::DirField *ui;
    QString        dirName_;
};

#endif // QT_DIRFIELD_HPP
