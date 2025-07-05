// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Base64 Encoding Tests
 * 
 * These tests validate Base64 encoding/decoding functionality which is used in
 * CLORE for RPC APIs and data serialization:
 * 
 * 1. RPC API Data: Binary data encoding in JSON-RPC responses
 * 2. Configuration Files: Encoding binary data in config files
 * 3. Network Communication: HTTP API data transmission
 * 4. Debugging/Logging: Binary data representation in logs
 * 5. Data Serialization: Cross-system data interchange
 * 
 * WHY RELEVANT FOR CLORE:
 * - RPC APIs need to encode binary transaction data in JSON responses
 * - HTTP APIs use Base64 for transmitting binary data safely
 * - Configuration files may store binary data in Base64 format
 * - Debugging tools need readable representation of binary data
 * - Ensures data integrity during text-based transmission
 * 
 * SECURITY NOTE: While not consensus-critical, encoding errors could:
 * - Corrupt RPC API responses causing client failures
 * - Cause data loss during configuration file processing
 * - Lead to communication errors with external systems
 * - Break debugging and diagnostic tools
 */

#include "utilstrencodings.h"   // Base64 encoding utilities
#include "test/test_clore.h"   // CLORE test framework

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(base64_tests, BasicTestingSetup)

    /**
     * TEST: Base64 Encoding/Decoding with Standard Test Vectors
     * 
     * IMPORTANCE: Validates that Base64 encoding produces correct results
     * and that decoding is the perfect inverse of encoding.
     * 
     * Tests both encoding and decoding with standard test vectors to ensure:
     * - Correct encoding of various input lengths (including edge cases)
     * - Perfect reversibility (encode->decode == original)
     * - Compliance with Base64 specification (RFC 4648)
     */
    BOOST_AUTO_TEST_CASE(base64_testvectors_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Base64 Encoding/Decoding ===");

        // Standard Base64 test vectors (RFC 4648 compatible)
        static const std::string vstrIn[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
        static const std::string vstrOut[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
        
        for (unsigned int i = 0; i < sizeof(vstrIn) / sizeof(vstrIn[0]); i++)
        {
            // TEST 1: Verify encoding produces expected output
            std::string strEnc = EncodeBase64(vstrIn[i]);
            BOOST_CHECK(strEnc == vstrOut[i]);
            
            // TEST 2: Verify decoding produces original input (reversibility)
            std::string strDec = DecodeBase64(strEnc);
            BOOST_CHECK(strDec == vstrIn[i]);
        }
        
        BOOST_TEST_MESSAGE("✓ Base64 encoding/decoding verified with standard test vectors");
        BOOST_TEST_MESSAGE("✓ Perfect reversibility confirmed (encode->decode == original)");
        BOOST_TEST_MESSAGE("=== Base64 Encoding Tests PASSED ===");
    }

BOOST_AUTO_TEST_SUITE_END()
