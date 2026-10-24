#ifndef QT_PAGEDDIALOG_HPP
#define QT_PAGEDDIALOG_HPP

#include <QDialog>
#include <QVector>

namespace Ui {
class PagedDialog;
}

class QStackedWidget;

/* A dialog of pages chosen from a list beside them, with the name and icon
   of the page shown above it: the settings and the preferences. */
class PagedDialog : public QDialog {
    Q_OBJECT

public:
    /* A page's name, translated when shown, and the name of its icon in
       the settings icons. */
    struct Page {
        const char *name;
        const char *icon;
    };

protected:
    /* The dialog opens at the size its pages need, but no smaller than the
       size given, and can be resized. */
    PagedDialog(const QString &title, const QSize &defaultSize, const QVector<Page> &pages,
                QWidget *parent = nullptr);
    ~PagedDialog() override;

    QStackedWidget *pageStack() const;
    /* Selects the first page, once the dialog has added its pages. */
    void showFirstPage();
    /* Called before the page at a list index is shown. */
    virtual void aboutToShowPage(int index);
    /* Grows the dialog if a page added after it was shown needs more room. */
    void growToFitPages();

    void showEvent(QShowEvent *event) override;

private:
    Ui::PagedDialog *ui;
    QSize            defaultSize;
    bool             sized = false;
};

#endif // QT_PAGEDDIALOG_HPP
