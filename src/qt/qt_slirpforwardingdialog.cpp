/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          SLiRP port forwarding configuration dialog.
 *
 *          Collects the rules as accepted; the caller writes them to the
 *          configuration file section net_slirp.c reads on initialization,
 *          when its settings are saved.
 *
 * Authors: chun-awa <chun-awa@lingmo.org>
 *
 *          Copyright 2026 chun-awa
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/config.h>
#include <86box/ini.h>
#include <86box/network.h>
}

#include "qt_slirpforwardingdialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

static void
fwdCategory(int nicIndex, char *buf, size_t len)
{
    snprintf(buf, len, NET_SLIRP_FWD_SECTION, nicIndex + 1);
}

SlirpForwardingDialog::SlirpForwardingDialog(int nicIndex, QWidget *parent)
    : QDialog(parent)
    , nicIndex(nicIndex)
{
    char category[40];
    char key[20];
    char tcp[] = "tcp";

    fwdCategory(nicIndex, category, sizeof(category));

    /* Load the current rules, normalizing them the way net_slirp.c does:
       a rule with only one of its ports set forwards that port number to
       itself, and the list ends at the first index with no ports at all. */
    for (int i = 0; i < NET_SLIRP_FWD_MAX; i++) {
        snprintf(key, sizeof(key), "%d_protocol", i);
        const int protocol = !strcmp(config_get_string(category, key, tcp), "udp");
        snprintf(key, sizeof(key), "%d_external", i);
        int external = config_get_int(category, key, 0);
        snprintf(key, sizeof(key), "%d_internal", i);
        int internal = config_get_int(category, key, 0);
        if ((external <= 0) && (internal <= 0))
            break;
        if (external <= 0)
            external = internal;
        else if (internal <= 0)
            internal = external;

        Rule rule = { protocol, external, internal };
        original.push_back(rule);
    }

    setWindowTitle(tr("SLiRP Port Forwarding (Adapter %1)").arg(nicIndex + 1));

    table = new QTableWidget(this);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({ tr("Protocol"), tr("External port"), tr("Internal port") });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (const Rule &rule : original)
        addRow(rule);

    auto *buttonAdd = new QPushButton(tr("Add"), this);
    buttonRemove    = new QPushButton(tr("Remove"), this);
    connect(buttonAdd, &QPushButton::clicked, this, &SlirpForwardingDialog::addRule);
    connect(buttonRemove, &QPushButton::clicked, this, &SlirpForwardingDialog::removeRule);

    auto *buttonsRow = new QHBoxLayout();
    buttonsRow->addWidget(buttonAdd);
    buttonsRow->addWidget(buttonRemove);
    buttonsRow->addStretch();

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(table);
    layout->addLayout(buttonsRow);
    layout->addWidget(buttonBox);

    updateButtons();
    resize(400, 360);
}

void
SlirpForwardingDialog::addRule()
{
    if (table->rowCount() >= NET_SLIRP_FWD_MAX)
        return;

    addRow({ 0, 8080, 80 });
    updateButtons();
}

void
SlirpForwardingDialog::removeRule()
{
    int row = table->currentRow();
    if (row < 0)
        row = table->rowCount() - 1;
    if (row >= 0)
        table->removeRow(row);

    updateButtons();
}

void
SlirpForwardingDialog::updateButtons()
{
    if (buttonRemove)
        buttonRemove->setEnabled(table->rowCount() > 0);
}

void
SlirpForwardingDialog::addRow(const Rule &rule)
{
    const int row = table->rowCount();
    table->insertRow(row);

    auto *protocol = new QComboBox(table);
    protocol->addItem(tr("TCP"), 0);
    protocol->addItem(tr("UDP"), 1);
    protocol->setCurrentIndex(rule.protocol ? 1 : 0);
    table->setCellWidget(row, 0, protocol);

    for (int col = 1; col <= 2; col++) {
        auto *port = new QSpinBox(table);
        port->setRange(1, 65535);
        port->setValue(col == 1 ? rule.external : rule.internal);
        table->setCellWidget(row, col, port);
    }
}

std::vector<SlirpForwardingDialog::Rule>
SlirpForwardingDialog::rulesFromTable() const
{
    std::vector<Rule> rules;

    rules.reserve(table->rowCount());
    for (int row = 0; row < table->rowCount(); row++) {
        auto *protocol = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
        auto *external = qobject_cast<QSpinBox *>(table->cellWidget(row, 1));
        auto *internal = qobject_cast<QSpinBox *>(table->cellWidget(row, 2));
        if (!protocol || !external || !internal)
            continue;

        Rule rule = { protocol->currentData().toInt(), external->value(), internal->value() };
        rules.push_back(rule);
    }

    return rules;
}

void
SlirpForwardingDialog::accept()
{
    /* Collect the rules only; the caller writes them from its settings
       save(), so canceling the settings discards them. */
    rulesNew  = rulesFromTable();
    m_changed = (rulesNew != original);

    QDialog::accept();
}

void
SlirpForwardingDialog::writeRules(int nicIndex, const std::vector<Rule> &rules)
{
    char category[40];
    char key[20];

    fwdCategory(nicIndex, category, sizeof(category));

    /* Rewrite the section from scratch, so that removed rules, and any
       rules left past a gap in the indices (which net_slirp.c ignores),
       disappear. */
    for (int i = 0; i < NET_SLIRP_FWD_MAX; i++) {
        snprintf(key, sizeof(key), "%d_protocol", i);
        config_delete_var(category, key);
        snprintf(key, sizeof(key), "%d_external", i);
        config_delete_var(category, key);
        snprintf(key, sizeof(key), "%d_internal", i);
        config_delete_var(category, key);
    }
    for (size_t i = 0; i < rules.size(); i++) {
        snprintf(key, sizeof(key), "%d_protocol", (int) i);
        config_set_string(category, key, rules[i].protocol ? "udp" : "tcp");
        snprintf(key, sizeof(key), "%d_external", (int) i);
        config_set_int(category, key, rules[i].external);
        snprintf(key, sizeof(key), "%d_internal", (int) i);
        config_set_int(category, key, rules[i].internal);
    }
    if (rules.empty()) {
        ini_section_t section = config_find_section(category);
        if (section)
            ini_delete_section_if_empty(config_get_ini(), section);
    }
}
