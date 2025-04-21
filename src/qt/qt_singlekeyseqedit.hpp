#ifndef SINGLEKEYSEQUENCEEDIT_H
#define SINGLEKEYSEQUENCEEDIT_H

#include <QKeySequenceEdit>
#include <QWidget>

class singleKeySequenceEdit : public QKeySequenceEdit
{
    Q_OBJECT
public:
    singleKeySequenceEdit(QWidget *parent = nullptr);

    void keyPressEvent(QKeyEvent *) override;
};

#endif // SINGLEKEYSEQUENCEEDIT_H
