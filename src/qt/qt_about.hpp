#ifndef QT_ABOUT_HPP
#define QT_ABOUT_HPP

#include <QMessageBox>

class About final : public QMessageBox {
    Q_OBJECT

public:
    explicit About(QWidget *parent = nullptr);
    ~About() override;
};
#endif // QT_ABOUT_HPP