// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Amount and Fee Validation Tests
 * 
 * These tests validate monetary amount handling and fee calculations which are 
 * CRITICAL for CLORE's economic security and transaction processing:
 * 
 * 1. Money Range Validation: Prevents integer overflow attacks and invalid amounts
 * 2. Fee Rate Calculations: Ensures accurate transaction fee computation
 * 3. Operator Safety: Validates arithmetic operations on monetary values
 * 4. Display Formatting: Ensures consistent monetary value representation
 * 
 * WHY THIS MATTERS FOR CLORE:
 * - Invalid amounts could crash nodes or enable double-spending attacks
 * - Incorrect fees could make transactions unprocessable or exploitable
 * - Validator nodes must accurately validate transaction economics
 * - Fee calculations affect network incentives and mining profitability
 * - Overflow bugs could create or destroy CLORE out of thin air
 * 
 * SECURITY IMPLICATIONS:
 * - Integer overflow could allow creation of infinite coins
 * - Fee miscalculations could enable transaction spam or DoS attacks
 * - Validator consensus depends on identical fee calculations across nodes
 * - Economic attacks target monetary validation edge cases
 */

#include "amount.h"              // Monetary amount types and constants
#include "policy/feerate.h"      // Fee calculation utilities
#include "test/test_clore.h"    // CLORE test framework

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(amount_tests, BasicTestingSetup)

    /**
     * TEST: Money Range Validation
     * 
     * IMPORTANCE: Validates that monetary amounts stay within safe bounds
     * - Negative amounts rejected (prevents negative balance exploits)
     * - Amounts above MAX_MONEY rejected (prevents inflation attacks)
     * - Valid amounts accepted for normal operation
     * 
     * WHY CRITICAL FOR CLORE: Without proper bounds checking:
     * - Attackers could create transactions with negative amounts
     * - Integer overflow could create unlimited CLORE supply
     * - Validator nodes could diverge on what constitutes valid amounts
     * - Economic model integrity would be compromised
     */
    BOOST_AUTO_TEST_CASE(Money_Range_Test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Money Range Validation ===");

        // TEST 1: Negative amounts must be rejected
        // SECURITY: Prevents negative balance exploits and impossible transactions
        BOOST_CHECK_EQUAL(MoneyRange(CAmount(-1)), false);
        BOOST_TEST_MESSAGE("✓ Negative amounts properly rejected");

        // TEST 2: Amounts exceeding MAX_MONEY must be rejected  
        // SECURITY: Prevents inflation attacks and integer overflow
        BOOST_CHECK_EQUAL(MoneyRange(MAX_MONEY + CAmount(1)), false);
        BOOST_TEST_MESSAGE("✓ Excessive amounts properly rejected (prevents inflation)");

        // TEST 3: Valid amounts within range should be accepted
        BOOST_CHECK_EQUAL(MoneyRange(CAmount(1)), true);
        BOOST_TEST_MESSAGE("✓ Valid amounts properly accepted");

        // Additional edge case tests for robustness
        BOOST_CHECK_EQUAL(MoneyRange(CAmount(0)), true);  // Zero should be valid
        BOOST_CHECK_EQUAL(MoneyRange(MAX_MONEY), true);   // Maximum should be valid
        BOOST_TEST_MESSAGE("✓ Edge cases (zero and maximum) handled correctly");

        BOOST_TEST_MESSAGE("=== Money Range Validation Tests PASSED ===");
    }

    /**
     * TEST: Fee Rate Calculation Logic
     * 
     * IMPORTANCE: Validates accurate fee computation for transaction processing
     * - Zero fee rates (free transactions)
     * - Positive fee rates (normal operation)
     * - Negative fee rates (edge case handling)
     * - Fractional fee calculations (precision handling)
     * - Constructor validation and copying
     * 
     * WHY CRITICAL FOR CLORE: Fee calculations must be:
     * - Identical across all validator nodes (consensus requirement)
     * - Resistant to precision errors (prevents divergence)
     * - Safe from overflow attacks (economic security)
     * - Accurate for transaction relay and mining incentives
     */
    BOOST_AUTO_TEST_CASE(Get_Fee_Test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Fee Rate Calculations ===");

        CFeeRate feeRate, altFeeRate;

        // TEST 1: Zero fee rate behavior
        // IMPORTANT: Free transactions should always have zero fees
        feeRate = CFeeRate(0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1e5), 0);
        BOOST_TEST_MESSAGE("✓ Zero fee rate produces zero fees for all sizes");

        // TEST 2: Simple proportional fee rate (1000 satoshis per 1000 bytes = 1:1)
        feeRate = CFeeRate(1000);
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1), 1);
        BOOST_CHECK_EQUAL(feeRate.GetFee(121), 121);
        BOOST_CHECK_EQUAL(feeRate.GetFee(999), 999);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), 1e3);
        BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), 9e3);
        BOOST_TEST_MESSAGE("✓ Proportional fee rate calculates correctly");

        // TEST 3: Negative fee rate handling (edge case)
        // NOTE: While unusual, negative rates should be handled consistently
        feeRate = CFeeRate(-1000);
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1), -1);
        BOOST_CHECK_EQUAL(feeRate.GetFee(121), -121);
        BOOST_CHECK_EQUAL(feeRate.GetFee(999), -999);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), -1e3);
        BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), -9e3);
        BOOST_TEST_MESSAGE("✓ Negative fee rates handled consistently");

        // TEST 4: Fractional fee rate calculation (precision testing)
        // CRITICAL: Rounding must be consistent across all nodes
        feeRate = CFeeRate(123);
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(8), 1);   // Special case: minimum fee
        BOOST_CHECK_EQUAL(feeRate.GetFee(9), 1);
        BOOST_CHECK_EQUAL(feeRate.GetFee(121), 14);
        BOOST_CHECK_EQUAL(feeRate.GetFee(122), 15);
        BOOST_CHECK_EQUAL(feeRate.GetFee(999), 122);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), 123);
        BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), 1107);
        BOOST_TEST_MESSAGE("✓ Fractional fee calculations with consistent rounding");

        // TEST 5: Negative fractional rates
        feeRate = CFeeRate(-123);
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(8), -1);  // Special case: minimum negative fee
        BOOST_CHECK_EQUAL(feeRate.GetFee(9), -1);
        BOOST_TEST_MESSAGE("✓ Negative fractional rates handled correctly");

        // TEST 6: Copy constructor validation
        // IMPORTANT: Fee rate objects must copy accurately
        feeRate = CFeeRate(1000);
        altFeeRate = CFeeRate(feeRate);
        BOOST_CHECK_EQUAL(feeRate.GetFee(100), altFeeRate.GetFee(100));
        BOOST_TEST_MESSAGE("✓ Fee rate copy constructor works correctly");

        // TEST 7: Full constructor with size parameter
        // Tests fee rate calculation from total fee and transaction size
        BOOST_CHECK(CFeeRate(CAmount(-1), 1000) == CFeeRate(-1));
        BOOST_CHECK(CFeeRate(CAmount(0), 1000) == CFeeRate(0));
        BOOST_CHECK(CFeeRate(CAmount(1), 1000) == CFeeRate(1));
        
        // TEST 8: Precision loss handling
        // CRITICAL: Must handle cases where precision is lost in division
        BOOST_CHECK(CFeeRate(CAmount(1), 1001) == CFeeRate(0));  // Lost precision
        BOOST_CHECK(CFeeRate(CAmount(2), 1001) == CFeeRate(1));  // Rounded up
        BOOST_TEST_MESSAGE("✓ Precision loss in fee calculation handled safely");

        // TEST 9: Integer calculation verification
        BOOST_CHECK(CFeeRate(CAmount(26), 789) == CFeeRate(32));
        BOOST_CHECK(CFeeRate(CAmount(27), 789) == CFeeRate(34));
        BOOST_TEST_MESSAGE("✓ Integer fee rate calculations verified");

        // TEST 10: Maximum value stress test
        // SECURITY: Ensure no crashes with maximum possible values
        CFeeRate(MAX_MONEY, std::numeric_limits<size_t>::max() >> 1).GetFeePerK();
        BOOST_TEST_MESSAGE("✓ Maximum value stress test passed (no crash)");

        BOOST_TEST_MESSAGE("=== Fee Rate Calculation Tests PASSED ===");
    }

    /**
     * TEST: Fee Rate Binary Operators
     * 
     * IMPORTANCE: Validates arithmetic and comparison operations on fee rates
     * - Comparison operators (<, >, ==, <=, >=)
     * - Addition operators (+=)
     * - Operator consistency and safety
     * 
     * WHY CRITICAL FOR CLORE: Fee rate operations must be:
     * - Mathematically consistent (prevents logic errors)
     * - Safe from overflow (prevents crashes)
     * - Identical across nodes (consensus requirement)
     */
    BOOST_AUTO_TEST_CASE(Binary_Operator_Test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Fee Rate Binary Operations ===");

        CFeeRate a, b;
        a = CFeeRate(1);
        b = CFeeRate(2);

        // TEST 1: Comparison operators
        BOOST_CHECK(a < b);   // Less than
        BOOST_CHECK(b > a);   // Greater than  
        BOOST_CHECK(a == a);  // Equality
        BOOST_TEST_MESSAGE("✓ Basic comparison operators work correctly");

        // TEST 2: Inequality operators
        BOOST_CHECK(a <= b);  // Less than or equal
        BOOST_CHECK(a <= a);  // Equal case
        BOOST_CHECK(b >= a);  // Greater than or equal
        BOOST_CHECK(b >= b);  // Equal case  
        BOOST_TEST_MESSAGE("✓ Inequality operators work correctly");

        // TEST 3: Addition operator
        // After a += a, 'a' should equal 'b' (both should be 2)
        a += a;
        BOOST_CHECK(a == b);
        BOOST_TEST_MESSAGE("✓ Addition operator produces correct results");

        BOOST_TEST_MESSAGE("=== Binary Operations Tests PASSED ===");
    }

    /**
     * TEST: Fee Rate String Representation
     * 
     * IMPORTANCE: Validates consistent display formatting for fee rates
     * - Correct unit display (CLORE/kB)
     * - Proper decimal precision
     * - Consistent formatting across systems
     * 
     * WHY IMPORTANT FOR CLORE: String representation affects:
     * - User interface consistency
     * - Log file interpretation  
     * - Debugging and troubleshooting
     * - API response formatting
     */
    BOOST_AUTO_TEST_CASE(ToString_Test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Fee Rate String Formatting ===");

        CFeeRate feeRate;
        feeRate = CFeeRate(1);
        
        // Verify correct CLORE unit display and precision
        BOOST_CHECK_EQUAL(feeRate.ToString(), "0.00000001 CLORE/kB");
        BOOST_TEST_MESSAGE("✓ Fee rate displays correctly as CLORE/kB with proper precision");

        BOOST_TEST_MESSAGE("=== String Formatting Tests PASSED ===");
    }

BOOST_AUTO_TEST_SUITE_END()
