#ifndef QT_SLIRPFORWARDINGDIALOG_HPP
#define QT_SLIRPFORWARDINGDIALOG_HPP

#include <QDialog>

#include <vector>

class QDialogButtonBox;
class QPushButton;
class QTableWidget;

/* Edits the SLiRP port forwarding rules of the network adapter slot
   nicIndex (0-based: the four adapters of the Network settings, then one
   per PC Card socket), stored in the "SLiRP Port Forwarding #<n>"
   configuration file section. The dialog itself never touches the
   configuration: the caller keeps the accepted rules and writes them
   with writeRules() from its settings save(), so canceling the settings
   discards them. The rules apply when SLiRP is next initialized. */
class SlirpForwardingDialog : public QDialog {
    Q_OBJECT

public:
    /* One forwarding rule: TCP or UDP, host ("external") and guest
       ("internal") port. */
    struct Rule {
        int protocol; /* 0 = TCP, 1 = UDP */
        int external;
        int internal;

        bool operator==(const Rule &other) const
        {
            return (protocol == other.protocol) && (external == other.external) && (internal == other.internal);
        }
    };

    explicit SlirpForwardingDialog(int nicIndex, QWidget *parent = nullptr);

    /* Whether the accepted rules differ from the ones the dialog was
       opened with. */
    bool changed() const { return m_changed; }

    /* The rules as accepted; only meaningful after accept(). */
    const std::vector<Rule> &rules() const { return rulesNew; }

    /* Rewrites the configuration section net_slirp.c reads on
       initialization. An empty list removes the section. */
    static void writeRules(int nicIndex, const std::vector<Rule> &rules);

public slots:
    void accept() override;

private slots:
    void addRule();
    void removeRule();
    void updateButtons();

private:
    void               addRow(const Rule &rule);
    std::vector<Rule>  rulesFromTable() const;

    int                nicIndex;
    bool               m_changed = false;
    std::vector<Rule>  original;
    std::vector<Rule>  rulesNew;
    QTableWidget      *table        = nullptr;
    QPushButton       *buttonRemove  = nullptr;
    QDialogButtonBox  *buttonBox     = nullptr;
};

#endif // QT_SLIRPFORWARDINGDIALOG_HPP
