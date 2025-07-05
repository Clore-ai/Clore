// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE P2PKH Script Tests - EXTREMELY CRITICAL
 * 
 * Tests Pay-to-Public-Key-Hash script identification and validation ensuring correct parsing of the most common CLORE transaction type.
 * Essential for wallet operations, transaction processing, and UTXO management as P2PKH represents the majority of user transactions.
 * 
 * IMPACT SUMMARY:
 * - Security: Prevents script misclassification that could lead to transaction validation failures and fund loss
 * - Performance: Ensures efficient P2PKH detection for fast transaction processing and wallet operations
 * - Users: Enables standard wallet transactions, address generation, and all basic CLORE send/receive functionality
 * - Business: Critical for exchange deposits/withdrawals, payment processing, and all standard CLORE transaction operations
 */

#include "script/script.h"
#include "test/test_clore.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(script_P2PKH_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(ispaytopublickeyhash_test)
    {
        BOOST_TEST_MESSAGE("Running IsPayToPubKeyHash Test");

        // Test CScript::IsPayToPublicKeyHash()
        uint160 dummy;
        CScript p2pkh;
        p2pkh << OP_DUP << OP_HASH160 << ToByteVector(dummy) << OP_EQUALVERIFY << OP_CHECKSIG;
        BOOST_CHECK(p2pkh.IsPayToPublicKeyHash());

        static const unsigned char direct[] = {
                OP_DUP, OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUALVERIFY, OP_CHECKSIG
        };
        BOOST_CHECK(CScript(direct, direct + sizeof(direct)).IsPayToPublicKeyHash());

        static const unsigned char notp2pkh1[] = {
                OP_DUP, OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUALVERIFY, OP_CHECKSIG, OP_CHECKSIG
        };
        BOOST_CHECK(!CScript(notp2pkh1, notp2pkh1 + sizeof(notp2pkh1)).IsPayToPublicKeyHash());

        static const unsigned char p2sh[] = {
                OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUAL
        };
        BOOST_CHECK(!CScript(p2sh, p2sh + sizeof(p2sh)).IsPayToPublicKeyHash());

        static const unsigned char extra[] = {
                OP_DUP, OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUALVERIFY, OP_CHECKSIG, OP_CHECKSIG
        };
        BOOST_CHECK(!CScript(extra, extra + sizeof(extra)).IsPayToPublicKeyHash());

        static const unsigned char missing[] = {
                OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUALVERIFY, OP_CHECKSIG, OP_RETURN
        };
        BOOST_CHECK(!CScript(missing, missing + sizeof(missing)).IsPayToPublicKeyHash());

        static const unsigned char missing2[] = {
                OP_DUP, OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };

        #pragma GCC diagnostic ignored "-Warray-bounds"
        BOOST_CHECK(!CScript(missing2, missing2 + sizeof(missing)).IsPayToPublicKeyHash());

        static const unsigned char tooshort[] = {
                OP_DUP, OP_HASH160, 2, 0, 0, OP_EQUALVERIFY, OP_CHECKSIG
        };

        #pragma GCC diagnostic ignored "-Warray-bounds"
        BOOST_CHECK(!CScript(tooshort, tooshort + sizeof(direct)).IsPayToPublicKeyHash());

    }

BOOST_AUTO_TEST_SUITE_END()
