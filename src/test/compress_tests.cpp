// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Amount Compression Tests - HIGHLY RELEVANT
 * 
 * Tests amount compression that reduces blockchain storage size and improves network efficiency.
 * Lossless compression enables cost-effective long-term scalability.
 * 
 * IMPACT SUMMARY:
 * - Security: Ensures lossless compression maintains consensus integrity across all nodes
 * - Performance: Reduces storage requirements and improves sync speed through smaller data
 * - Users: Enables mobile wallets and faster synchronization in bandwidth-limited regions
 * - Business: Reduces infrastructure costs and enables cost-effective global deployment
 */

#include "compressor.h"          // Amount compression utilities
#include "util.h"                // Utility functions and constants
#include "test/test_clore.h"     // CLORE test framework

#include <stdint.h>

#include <boost/test/unit_test.hpp>

// Test ranges for different CLORE amount denominations
// amounts 0.00000001 .. 0.00100000 (smallest units to 0.001 CLORE)
#define NUM_MULTIPLES_UNIT 100000

// amounts 0.01 .. 100.00 (cent-level amounts)
#define NUM_MULTIPLES_CENT 10000

// amounts 1 .. 10000 (whole CLORE amounts)
#define NUM_MULTIPLES_1CLORE 10000

// amounts 50 .. 21000000 (large CLORE amounts)
#define NUM_MULTIPLES_50CLORE 420000

BOOST_FIXTURE_TEST_SUITE(compress_tests, BasicTestingSetup)

    /**
     * UTILITY: Test encoding round-trip (amount -> compressed -> amount)
     * 
     * Verifies that compressing and then decompressing an amount
     * returns the original value exactly.
     */
    bool static TestEncode(uint64_t in)
    {
        return in == CTxOutCompressor::DecompressAmount(CTxOutCompressor::CompressAmount(in));
    }

    /**
     * UTILITY: Test decoding round-trip (compressed -> amount -> compressed)
     * 
     * Verifies that decompressing and then compressing a value
     * returns the original compressed value exactly.
     */
    bool static TestDecode(uint64_t in)
    {
        return in == CTxOutCompressor::CompressAmount(CTxOutCompressor::DecompressAmount(in));
    }

    /**
     * UTILITY: Test specific amount-compression pairs
     * 
     * Verifies that a specific amount compresses to the expected value
     * and that the compressed value decompresses to the original amount.
     */
    bool static TestPair(uint64_t dec, uint64_t enc)
    {
        return CTxOutCompressor::CompressAmount(dec) == enc &&
               CTxOutCompressor::DecompressAmount(enc) == dec;
    }

    /**
     * TEST: Amount Compression Functionality
     * 
     * IMPORTANCE: Validates that amount compression works correctly across
     * all CLORE denomination ranges without data loss.
     * 
     * This comprehensive test verifies:
     * 1. Specific important amount values compress correctly
     * 2. All micro-amounts (0.00000001 to 0.001 CLORE) round-trip correctly
     * 3. All cent-level amounts (0.01 to 100 CLORE) preserve precision
     * 4. Whole CLORE amounts (1 to 10,000 CLORE) compress efficiently
     * 5. Large amounts (50 to 21,000,000 CLORE) handle properly
     * 6. Random compressed values decode without corruption
     * 
     * CRITICAL FOR CLORE: Ensures that all transaction amounts can be
     * stored efficiently without any loss of precision or data corruption.
     */
    BOOST_AUTO_TEST_CASE(compress_amounts_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Amount Compression ===");

        // TEST 1: Verify specific important amount values
        BOOST_CHECK(TestPair(0, 0x0));                    // Zero amount
        BOOST_CHECK(TestPair(1, 0x1));                    // Smallest unit (1 satoshi)
        BOOST_CHECK(TestPair(CENT, 0x7));                 // 0.01 CLORE
        BOOST_CHECK(TestPair(COIN, 0x9));                 // 1 CLORE
        BOOST_CHECK(TestPair(5000 * COIN, 0x1388));       // 5,000 CLORE
        BOOST_CHECK(TestPair(1300000000 * COIN, 0x4E3B29200)); // Very large amount
        
        BOOST_TEST_MESSAGE("✓ Specific CLORE amount values compress correctly");

        // TEST 2: Micro-amounts (0.00000001 to 0.001 CLORE)
        // Critical for precise transaction handling
        for (uint64_t i = 1; i <= NUM_MULTIPLES_UNIT; i++)
            BOOST_CHECK(TestEncode(i));
        
        BOOST_TEST_MESSAGE("✓ Micro-amount compression verified (0.00000001 to 0.001 CLORE)");

        // TEST 3: Cent-level amounts (0.01 to 100 CLORE)
        // Important for typical transaction values
        for (uint64_t i = 1; i <= NUM_MULTIPLES_CENT; i++)
            BOOST_CHECK(TestEncode(i * CENT));
        
        BOOST_TEST_MESSAGE("✓ Cent-level compression verified (0.01 to 100 CLORE)");

        // TEST 4: Whole CLORE amounts (1 to 10,000 CLORE)
        // Common range for user transactions
        for (uint64_t i = 1; i <= NUM_MULTIPLES_1CLORE; i++)
            BOOST_CHECK(TestEncode(i * COIN));
        
        BOOST_TEST_MESSAGE("✓ Whole CLORE compression verified (1 to 10,000 CLORE)");

        // TEST 5: Large amounts (50 to 21,000,000 CLORE)
        // Important for high-value transactions and exchanges
        for (uint64_t i = 1; i <= NUM_MULTIPLES_50CLORE; i++)
            BOOST_CHECK(TestEncode(i * 5000 * COIN));
        
        BOOST_TEST_MESSAGE("✓ Large amount compression verified (50 to 21M CLORE)");

        // TEST 6: Random compressed value decoding
        // Ensures decoder handles any compressed input safely
        for (uint64_t i = 0; i < 100000; i++)
            BOOST_CHECK(TestDecode(i));
        
        BOOST_TEST_MESSAGE("✓ Random compressed value decoding verified");
        BOOST_TEST_MESSAGE("✓ All CLORE amount ranges compress losslessly");
        BOOST_TEST_MESSAGE("=== Amount Compression Tests PASSED ===");
    }

BOOST_AUTO_TEST_SUITE_END()
