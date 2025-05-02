// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2022 The CLORE.AI
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_SCRIPT_ISMINE_H
#define CLORE_SCRIPT_ISMINE_H

#include "script/standard.h"

#include <stdint.h>

class CKeyStore;
class CScript;

/** IsMine() return codes */
enum isminetype
{
    ISMINE_NO = 0,
    //! Indicates that we don't know how to create a scriptSig that would solve this if we were given the appropriate private keys
    ISMINE_WATCH_UNSOLVABLE = 1,
    //! Indicates that we know how to create a scriptSig that would solve this if we were given the appropriate private keys
    ISMINE_WATCH_SOLVABLE = 2,
    ISMINE_WATCH_ONLY = ISMINE_WATCH_SOLVABLE | ISMINE_WATCH_UNSOLVABLE,
    ISMINE_SPENDABLE = 4,
    ISMINE_COLD = 5,

    //! Indicates that we have the spending key of a P2CS
    ISMINE_SPENDABLE_DELEGATED = 1 << 6,
    //! Indicates that we don't have the spending key of a shielded spend/output
    ISMINE_WATCH_ONLY_SHIELDED = 1 << 7,
    //! Indicates that we have the spending key of a shielded spend/output.
    ISMINE_SPENDABLE_SHIELDED = 1 << 8,
    ISMINE_SPENDABLE_TRANSPARENT = ISMINE_SPENDABLE_DELEGATED | ISMINE_SPENDABLE,
    ISMINE_SPENDABLE_NO_DELEGATED =  ISMINE_SPENDABLE | ISMINE_SPENDABLE_SHIELDED,
    ISMINE_SPENDABLE_ALL = ISMINE_SPENDABLE_DELEGATED | ISMINE_SPENDABLE | ISMINE_SPENDABLE_SHIELDED,
    ISMINE_WATCH_ONLY_ALL = ISMINE_WATCH_ONLY | ISMINE_WATCH_ONLY_SHIELDED,
    ISMINE_ALL = ISMINE_WATCH_ONLY | ISMINE_SPENDABLE | ISMINE_COLD | ISMINE_SPENDABLE_DELEGATED | ISMINE_SPENDABLE_SHIELDED | ISMINE_WATCH_ONLY_SHIELDED,
    ISMINE_ENUM_ELEMENTS    
};
/** used for bitflags of isminetype */
typedef uint8_t isminefilter;

/* isInvalid becomes true when the script is found invalid by consensus or policy. This will terminate the recursion
 * and return a ISMINE_NO immediately, as an invalid script should never be considered as "mine". This is needed as
 * different SIGVERSION may have different network rules. Currently the only use of isInvalid is indicate uncompressed
 * keys in SIGVERSION_WITNESS_V0 script, but could also be used in similar cases in the future
 */
isminetype IsMine(const CKeyStore& keystore, const CScript& scriptPubKey, bool& isInvalid, SigVersion = SIGVERSION_BASE);
isminetype IsMine(const CKeyStore& keystore, const CScript& scriptPubKey, SigVersion = SIGVERSION_BASE);
isminetype IsMine(const CKeyStore& keystore, const CTxDestination& dest, bool& isInvalid, SigVersion = SIGVERSION_BASE);
isminetype IsMine(const CKeyStore& keystore, const CTxDestination& dest, SigVersion = SIGVERSION_BASE);

#endif // CLORE_SCRIPT_ISMINE_H
