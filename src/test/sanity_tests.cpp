// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE System Sanity Tests - EXTREMELY CRITICAL
 * 
 * Tests fundamental system library compatibility including C/C++ standard libraries and OpenSSL cryptographic functions.
 * Essential validation ensuring the underlying system can safely execute cryptographic operations required for CLORE's security.
 * 
 * IMPACT SUMMARY:
 * - Security: Validates OpenSSL ECC functionality preventing cryptographic failures that could compromise all private key operations
 * - Performance: Ensures system libraries function correctly for optimal node performance and stability
 * - Users: Prevents wallet corruption and transaction failures by verifying system compatibility before node startup
 * - Business: Critical prerequisite for exchange and service deployment ensuring reliable cryptographic infrastructure
 */

#include "compat/sanity.h"
#include "key.h"
#include "test/test_clore.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(sanity_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(basic_sanity_test)
    {
        BOOST_TEST_MESSAGE("Running Basic Sanity Test");

        BOOST_CHECK_MESSAGE(glibc_sanity_test() == true, "libc sanity test");
        BOOST_CHECK_MESSAGE(glibcxx_sanity_test() == true, "stdlib sanity test");
        BOOST_CHECK_MESSAGE(ECC_InitSanityCheck() == true, "openssl ECC test");
    }

BOOST_AUTO_TEST_SUITE_END()
