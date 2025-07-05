// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Base32 Encoding Tests
 * 
 * These tests validate Base32 encoding/decoding functionality which may be used in
 * CLORE for data serialization and user-facing string representations:
 * 
 * 1. Data Encoding: Converting binary data to human-readable strings
 * 2. Address Formats: Potential use in address encoding schemes
 * 3. API Responses: Consistent data representation in APIs
 * 4. Import/Export: Data interchange with other systems
 * 
 * WHY RELEVANT FOR CLORE:
 * - Ensures data integrity when converting between binary and string formats
 * - Validates reversible encoding (encode->decode must return original data)
 * - Important for user interface consistency and data export functionality
 * - Prevents corruption of data during string-based operations
 * 
 * SECURITY NOTE: While not consensus-critical, encoding errors could:
 * - Corrupt exported wallet data or transaction information
 * - Cause user interface display issues
 * - Lead to data loss during import/export operations
 */

#include "utilstrencodings.h"   // Base32 encoding utilities
#include "test/test_clore.h"   // CLORE test framework

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(base32_tests, BasicTestingSetup)

    /**
     * TEST: Base32 Encoding/Decoding with Standard Test Vectors
     * 
     * IMPORTANCE: Validates that Base32 encoding produces correct results
     * and that decoding is the perfect inverse of encoding.
     * 
     * Tests both encoding and decoding with standard test vectors to ensure:
     * - Correct encoding of various input lengths (including edge cases)
     * - Perfect reversibility (encode->decode == original)
     * - Compliance with Base32 specification
     */
    BOOST_AUTO_TEST_CASE(base32_testvectors_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Base32 Encoding/Decoding ===");

        // Standard Base32 test vectors (RFC 4648 compatible)
        static const std::string vstrIn[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
        static const std::string vstrOut[] = {"", "my======", "mzxq====", "mzxw6===", "mzxw6yq=", "mzxw6ytb", "mzxw6ytboi======"};
        
        for (unsigned int i = 0; i < sizeof(vstrIn) / sizeof(vstrIn[0]); i++)
        {
            // TEST 1: Verify encoding produces expected output
            std::string strEnc = EncodeBase32(vstrIn[i]);
            BOOST_CHECK(strEnc == vstrOut[i]);
            
            // TEST 2: Verify decoding produces original input (reversibility)
            std::string strDec = DecodeBase32(vstrOut[i]);
            BOOST_CHECK(strDec == vstrIn[i]);
        }
        
        BOOST_TEST_MESSAGE("✓ Base32 encoding/decoding verified with standard test vectors");
        BOOST_TEST_MESSAGE("✓ Perfect reversibility confirmed (encode->decode == original)");
        BOOST_TEST_MESSAGE("=== Base32 Encoding Tests PASSED ===");
    }

BOOST_AUTO_TEST_SUITE_END()
