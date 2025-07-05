// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Time Data Tests - MODERATE
 * 
 * Tests median filter functionality for network time synchronization and timestamp validation in blockchain operations.
 * Supports accurate time management for block validation, network synchronization, and preventing time-based attacks.
 * 
 * IMPACT SUMMARY:
 * - Security: Helps prevent time-based attacks by filtering outlier timestamps and maintaining network time consensus
 * - Performance: Enables efficient time synchronization filtering for stable network time coordination
 * - Users: Supports accurate transaction timestamps and reliable blockchain synchronization timing
 * - Business: Important for validator timing coordination, exchange operations, and time-sensitive blockchain functionality
 */

#include "timedata.h"
#include "test/test_clore.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(timedata_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(util_MedianFilter_test)
    {
        BOOST_TEST_MESSAGE("Running Util MedianFilter Test");

        CMedianFilter<int> filter(5, 15);

        BOOST_CHECK_EQUAL(filter.median(), 15);

        filter.input(20); // [15 20]
        BOOST_CHECK_EQUAL(filter.median(), 17);

        filter.input(30); // [15 20 30]
        BOOST_CHECK_EQUAL(filter.median(), 20);

        filter.input(3); // [3 15 20 30]
        BOOST_CHECK_EQUAL(filter.median(), 17);

        filter.input(7); // [3 7 15 20 30]
        BOOST_CHECK_EQUAL(filter.median(), 15);

        filter.input(18); // [3 7 18 20 30]
        BOOST_CHECK_EQUAL(filter.median(), 18);

        filter.input(0); // [0 3 7 18 30]
        BOOST_CHECK_EQUAL(filter.median(), 7);
    }

BOOST_AUTO_TEST_SUITE_END()
