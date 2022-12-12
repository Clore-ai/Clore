#ifndef CLORE_QT_TEST_WALLETTESTS_H
#define CLORE_QT_TEST_WALLETTESTS_H

#include <QObject>
#include <QTest>

class WalletTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void walletTests();
};

#endif // CLORE_QT_TEST_WALLETTESTS_H
