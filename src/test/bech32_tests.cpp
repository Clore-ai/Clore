// Copyright (c) 2017 Pieter Wuille
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Bech32 Encoding Tests
 * 
 * These tests validate Bech32 encoding/decoding functionality (BIP 173).
 * RELEVANCE TO CLORE: UNCERTAIN - depends on SegWit implementation status.
 * 
 * 1. SegWit Addresses: Bech32 is used for native SegWit addresses (bc1...)
 * 2. Future Compatibility: May be preparation for SegWit adoption
 * 3. Inherited Functionality: Could be present from Bitcoin codebase
 * 4. Address Validation: If used, must validate Bech32 addresses correctly
 * 
 * WHY THIS MIGHT BE RELEVANT FOR CLORE:
 * - If CLORE supports SegWit, users could have Bech32 addresses
 * - Future-proofing for potential SegWit upgrade
 * - Wallet compatibility with SegWit-enabled versions
 * - Exchange integration may require Bech32 support
 * 
 * WHY THIS MIGHT NOT BE RELEVANT:
 * - CLORE may not have implemented SegWit functionality
 * - Traditional Base58 addresses may be the only format supported
 * - Bech32 functionality may be unused inherited code
 * 
 * SECURITY NOTE: If Bech32 is used, encoding errors could:
 * - Cause loss of funds sent to malformed addresses
 * - Break wallet compatibility with SegWit features
 * - Cause validation failures for legitimate addresses
 * 
 * RECOMMENDATION: Verify if CLORE actually uses Bech32 addresses
 * before considering this test critical.
 */

#include "test/test_clore.h"    // CLORE test framework

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(bech32_tests, BasicTestingSetup)

    /**
     * UTILITY: Case-insensitive string comparison
     * 
     * Bech32 addresses are case-insensitive, so this utility
     * ensures proper comparison between encoded and decoded values.
     */
    bool CaseInsensitiveEqual(const std::string &s1, const std::string &s2)
    {
        if (s1.size() != s2.size()) return false;
        for (size_t i = 0; i < s1.size(); ++i)
        {
            char c1 = s1[i];
            if (c1 >= 'A' && c1 <= 'Z') c1 -= ('A' - 'a');
            char c2 = s2[i];
            if (c2 >= 'A' && c2 <= 'Z') c2 -= ('A' - 'a');
            if (c1 != c2) return false;
        }
        return true;
    }

    /**
     * TEST: BIP 173 Valid Test Vectors
     * 
     * IMPORTANCE: Validates that Bech32 encoding produces correct results
     * for known valid inputs as specified in BIP 173.
     * 
     * Tests encoding and decoding of valid Bech32 strings to ensure:
     * - Correct decoding of standard test vectors
     * - Perfect reversibility (decode->encode == original)
     * - Case insensitive operation
     */
    BOOST_AUTO_TEST_CASE(bip173_testvectors_valid)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Bech32 Valid Test Vectors ===");

        static const std::string CASES[] =
                {
                        "A12UEL5L",
                        "a12uel5l",
                        "an83characterlonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1tt5tgs",
                        "abcdef1qpzry9x8gf2tvdw0s3jn54khce6mua7lmqqqxw",
                        "11qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqc8247j",
                        "split1checkupstagehandshakeupstreamerranterredcaperred2y9e3w",
                        "?1ezyfcl",
                };
        for (const std::string &str : CASES)
        {
            // TEST 1: Decode valid Bech32 string
            auto ret = bech32::Decode(str);
            BOOST_CHECK(!ret.first.empty());
            
            // TEST 2: Re-encode and verify reversibility
            std::string recode = bech32::Encode(ret.first, ret.second);
            BOOST_CHECK(!recode.empty());
            BOOST_CHECK(CaseInsensitiveEqual(str, recode));
        }
        
        BOOST_TEST_MESSAGE("✓ All valid Bech32 test vectors passed");
        BOOST_TEST_MESSAGE("=== Bech32 Valid Tests PASSED ===");
    }

    /**
     * TEST: BIP 173 Invalid Test Vectors
     * 
     * IMPORTANCE: Validates that Bech32 decoding properly rejects
     * malformed inputs as specified in BIP 173.
     * 
     * Tests that invalid Bech32 strings are properly rejected to prevent:
     * - Acceptance of malformed addresses
     * - Potential security vulnerabilities
     * - Data corruption from invalid inputs
     */
    BOOST_AUTO_TEST_CASE(bip173_testvectors_invalid)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Bech32 Invalid Test Vectors ===");

        static const std::string CASES[] =
                {
                        " 1nwldj5",          // Leading space
                        "\x7f""1axkwrx",     // High bit character
                        "\x80""1eym55h",     // Invalid character
                        "an84characterslonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1569pvx", // Too long
                        "pzry9x0s0muk",      // No separator
                        "1pzry9x0s0muk",     // Empty HRP
                        "x1b4n0q5v",         // Invalid witness version
                        "li1dgmt3",          // Too short checksum
                        "de1lg7wt\xff",      // Invalid character in checksum
                        "A1G7SGD8",          // Checksum calculated with uppercase form
                        "10a06t8",           // Empty HRP
                        "1qzzfhee",          // Empty HRP
                };
        for (const std::string &str : CASES)
        {
            // TEST: Verify invalid strings are properly rejected
            auto ret = bech32::Decode(str);
            BOOST_CHECK(ret.first.empty());
        }
        
        BOOST_TEST_MESSAGE("✓ All invalid Bech32 test vectors properly rejected");
        BOOST_TEST_MESSAGE("=== Bech32 Invalid Tests PASSED ===");
    }

BOOST_AUTO_TEST_SUITE_END()
