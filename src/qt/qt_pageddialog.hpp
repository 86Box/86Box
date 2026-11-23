#ifndef QT_PAGEDDIALOG_HPP
#define QT_PAGEDDIALOG_HPP

#include <QDialog>
#include <QStackedWidget>
#include <QVector>

namespace Ui {
class PagedDialog;
}

/* Settings builds hidden pages when validating a save. Those pages must not
   change the size of the page the user is still looking at. */
class PagedDialogStack : public QStackedWidget {
public:
    explicit PagedDialogStack(QWidget *parent = nullptr);
    void setFitCurrentPage(bool enabled);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool  hasHeightForWidth() const override;
    int   heightForWidth(int width) const override;

private:
    bool fitCurrentPage = false;
};

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
    void setFitCurrentPage(bool enabled);
    /* Selects the first page, once the dialog has added its pages. */
    void showFirstPage();
    /* Selects the page at a list index, as a click on it in the list does. */
    void showPage(int index);
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
