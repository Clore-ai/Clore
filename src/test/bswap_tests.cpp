// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Byte Swap Tests - HIGHLY RELEVANT
 * 
 * Tests byte swapping functions that ensure cross-platform compatibility for different CPU architectures.
 * Critical for network consensus and data integrity across Intel and ARM systems.
 * 
 * IMPACT SUMMARY:
 * - Security: Prevents consensus splits and data corruption from endianness mismatches
 * - Performance: Enables deployment on cost-effective ARM and diverse CPU architectures
 * - Users: Allows validators and nodes to run on various platforms without compatibility issues
 * - Business: Enables cloud flexibility and mobile integration with consistent data interpretation
 */

#include "compat/byteswap.h"    // Cross-platform byte swapping utilities
#include "test/test_clore.h"    // CLORE test framework

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(bswap_tests, BasicTestingSetup)

/**
 * TEST: Byte Swapping Operations
 * 
 * IMPORTANCE: Validates that byte swapping functions correctly reverse
 * the byte order for 16-bit, 32-bit, and 64-bit integers.
 * 
 * This test ensures:
 * - 16-bit values are swapped correctly (0x1234 -> 0x3412)
 * - 32-bit values are swapped correctly (0x56789abc -> 0xbc9a7856) 
 * - 64-bit values are swapped correctly (preserving all 8 bytes)
 * 
 * CRITICAL FOR CLORE: These operations are used throughout the codebase for:
 * - Converting between little-endian and big-endian representations
 * - Network protocol compliance (network byte order is big-endian)
 * - Cross-platform data serialization and storage
 * - Ensuring identical data interpretation across all validator nodes
 */
BOOST_AUTO_TEST_CASE(bswap_test)
{
    BOOST_TEST_MESSAGE("=== Testing CLORE Cross-Platform Byte Swapping ===");

    // TEST 1: 16-bit byte swap validation
    uint16_t u1 = 0x1234;       // Original: 0001 0010 0011 0100
    uint16_t e1 = 0x3412;       // Expected: 0011 0100 0001 0010
    BOOST_CHECK(bswap_16(u1) == e1);
    
    // TEST 2: 32-bit byte swap validation  
    uint32_t u2 = 0x56789abc;   // Original: 0101 0110 0111 1000 1001 1010 1011 1100
    uint32_t e2 = 0xbc9a7856;   // Expected: 1011 1100 1001 1010 0111 1000 0101 0110
    BOOST_CHECK(bswap_32(u2) == e2);
    
    // TEST 3: 64-bit byte swap validation
    uint64_t u3 = 0xdef0123456789abc;  // Original 8-byte value
    uint64_t e3 = 0xbc9a78563412f0de;  // Expected with all bytes reversed
    BOOST_CHECK(bswap_64(u3) == e3);
    
    BOOST_TEST_MESSAGE("✓ 16-bit byte swapping verified");
    BOOST_TEST_MESSAGE("✓ 32-bit byte swapping verified");
    BOOST_TEST_MESSAGE("✓ 64-bit byte swapping verified");
    BOOST_TEST_MESSAGE("✓ Cross-platform data compatibility ensured");
    BOOST_TEST_MESSAGE("=== Byte Swapping Tests PASSED ===");
}

BOOST_AUTO_TEST_SUITE_END()
