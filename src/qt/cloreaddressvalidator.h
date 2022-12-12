// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2022 The CLORE.AI
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_QT_CLOREADDRESSVALIDATOR_H
#define CLORE_QT_CLOREADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class CloreAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit CloreAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** clore address widget validator, checks for a valid clore address.
 */
class CloreAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit CloreAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // CLORE_QT_CLOREADDRESSVALIDATOR_H
