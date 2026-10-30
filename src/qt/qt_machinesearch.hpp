#ifndef QT_MACHINESEARCH_HPP
#define QT_MACHINESEARCH_HPP

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QProgressDialog;
class QSpinBox;
class QTreeWidget;

class MachineSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit MachineSearchDialog(QWidget *parent = nullptr);

    /* Goes through the machine table, which probes each machine's ROMs;
       the progress dialog follows it, one step per machine. Returns false
       if the user cancelled it. */
    bool load(QProgressDialog *progress);

    /* The machine chosen in the results, or -1. */
    int selectedMachine() const;

private:
    struct FlagFilter {
        QCheckBox *checkBox;
        uintptr_t  flags;
        bool       bus;
    };

    void addFlagFilter(QGridLayout *grid, const QString &text, uintptr_t flags, bool bus);
    void hideUnusedBuses();
    void fillDeviceCombo(QComboBox *comboBox, const QVector<const void *> &devices);
    bool matches(int m) const;
    void updateResults();
    void reset();

    QLineEdit   *lineEditName;
    QComboBox   *comboBoxType;
    QComboBox   *comboBoxChipset;
    QComboBox   *comboBoxVideo;
    QComboBox   *comboBoxSound;
    QComboBox   *comboBoxNet;
    QComboBox   *comboBoxSCSI;
    QSpinBox    *spinBoxRAM;
    QGridLayout *busGrid;
    QTreeWidget *treeResults;
    QLabel      *labelCount;

    QVector<FlagFilter> flagFilters;
    /* Machines whose ROMs are present, in table order. */
    QVector<int>        available;
};

#endif // QT_MACHINESEARCH_HPP
