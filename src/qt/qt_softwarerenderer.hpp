#ifndef SOFTWARERENDERER_HPP
#define SOFTWARERENDERER_HPP

#include <QWidget>

class SoftwareRenderer : public QWidget
{
    Q_OBJECT
public:
    explicit SoftwareRenderer(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;

public slots:
    void onBlit(const QImage& img, int, int, int, int);

private:
    QImage image;
    int sx, sy, sw, sh;
};

#endif // SOFTWARERENDERER_HPP
