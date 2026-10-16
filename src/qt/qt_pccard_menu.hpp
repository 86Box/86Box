/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             The PC Card menu, behind the PC Card icon in the status bar:
 *             put any card into either socket, or take it out, while the
 *             machine runs.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef QT_PCCARD_MENU_HPP
#define QT_PCCARD_MENU_HPP

#include <QObject>
#include <QString>

class QMenu;
class QWidget;

class PcCardMenu : public QObject {
    Q_OBJECT

public:
    explicit PcCardMenu(QWidget *parent);

    QMenu  *menu() const { return m_menu; }
    QString toolTip() const;   /* the sockets and what is in them */

signals:
    void changed();

private:
    void buildMenu();

    QMenu *m_menu;
};

#endif // QT_PCCARD_MENU_HPP
