// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE P2PK Script Tests - HIGHLY RELEVANT
 * 
 * Tests Pay-to-Public-Key script identification and validation supporting both compressed and uncompressed public key formats.
 * Important for backwards compatibility, miner coinbase transactions, and specialized use cases requiring direct public key payments.
 * 
 * IMPACT SUMMARY:
 * - Security: Ensures correct validation of P2PK transactions preventing script misclassification and validation failures
 * - Performance: Enables efficient P2PK script detection for comprehensive transaction type support
 * - Users: Supports legacy addresses and specialized wallet configurations that may use direct public key payments
 * - Business: Required for mining pool compatibility, exchange legacy support, and comprehensive transaction type handling
 */

#include "script/script.h"
#include "test/test_clore.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(script_P2PK_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(ispaytopublickey_test)
    {
        BOOST_TEST_MESSAGE("Running IsPayToPublicKey Test");

        // Test CScript::IsPayToPublicKey()
        static const unsigned char p2pkcompressedeven[] = {
                0x41, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_CHECKSIG
        };
        BOOST_CHECK(CScript(p2pkcompressedeven, p2pkcompressedeven + sizeof(p2pkcompressedeven)).IsPayToPublicKey());

        static const unsigned char p2pkcompressedodd[] = {
                0x41, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_CHECKSIG
        };
        BOOST_CHECK(CScript(p2pkcompressedodd, p2pkcompressedodd + sizeof(p2pkcompressedodd)).IsPayToPublicKey());

        static const unsigned char p2pkuncompressed[] = {
                0x41, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_CHECKSIG
        };
        BOOST_CHECK(CScript(p2pkuncompressed, p2pkuncompressed + sizeof(p2pkuncompressed)).IsPayToPublicKey());

        static const unsigned char missingop[] = {
                0x41, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        BOOST_CHECK(!CScript(missingop, missingop + sizeof(missingop)).IsPayToPublicKey());

        static const unsigned char wrongop[] = {
                0x41, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUALVERIFY
        };
        BOOST_CHECK(!CScript(wrongop, wrongop + sizeof(wrongop)).IsPayToPublicKey());

        static const unsigned char tooshort[] = {
                0x41, 0x02, 0, 0, OP_CHECKSIG
        };
        BOOST_CHECK(!CScript(tooshort, tooshort + sizeof(tooshort)).IsPayToPublicKey());

    }

BOOST_AUTO_TEST_SUITE_END()
