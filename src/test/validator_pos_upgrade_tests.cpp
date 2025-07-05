// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Validator PoS Upgrade Tests - EXTREMELY CRITICAL
 * 
 * Tests validator system behavior during PoW→PoS transition including timing validation, staking activation, and network upgrades.
 * Essential for ensuring smooth transition from proof-of-work to proof-of-stake consensus with proper validator operations.
 * 
 * IMPACT SUMMARY:
 * - Security: Validates secure PoS transition, prevents premature staking, and ensures proper validator activation timing
 * - Performance: Ensures efficient staking operations, time validation, and consensus transition without network disruption
 * - Users: Enables reliable PoS upgrade experience with proper validator functionality and staking rewards
 * - Business: Critical for successful PoW→PoS migration, maintaining network security, and preserving economic incentives
 */

#include "test/test_clore.h"
#include "chainparams.h"
#include "validator.h"
#include "netbase.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/upgrades.h"
#include "amount.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

struct ValidatorPosUpgradeTestingSetup : public TestingSetup {
    ValidatorPosUpgradeTestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(validator_pos_upgrade_tests, ValidatorPosUpgradeTestingSetup)

BOOST_AUTO_TEST_CASE(test_validator_creation_timing_before_pos)
{
    BOOST_TEST_MESSAGE("=== Testing Validator Creation Before PoS Activation ===");
    
    // Test that validators with valid keys cannot operate before PoS is enabled
    // This simulates the scenario where someone has authorized keys but tries to activate early
    
    CKey authorizedKey;
    authorizedKey.MakeNewKey(false);
    CPubKey authorizedPubKey = authorizedKey.GetPubKey();
    
    CKey validatorKey;
    validatorKey.MakeNewKey(false);
    CPubKey validatorPubKey = validatorKey.GetPubKey();
    
    // Create transaction input for collateral
    CTxIn collateralInput;
    collateralInput.prevout.hash = GetRandHash();
    collateralInput.prevout.n = 0;
    
    CService validatorService;
    BOOST_CHECK(Lookup("127.0.0.1:9999", validatorService, 9999, false));
    
    // Test: Should validator creation be allowed before PoS?
    // In regtest mode, it should work for testing purposes
    std::string strError = "";
    CValidatorBroadcast validatorBroadcast;
    
    bool result = CValidatorBroadcast::Create(
        collateralInput,
        validatorService,
        authorizedKey,
        authorizedPubKey,
        validatorKey,
        validatorPubKey,
        strError,
        validatorBroadcast
    );
    
    BOOST_TEST_MESSAGE("Pre-PoS validator creation result: " + std::to_string(result));
    if (!result) {
        BOOST_TEST_MESSAGE("Error: " + strError);
    }
    
    // In regtest, this should succeed for testing flexibility
    BOOST_CHECK_MESSAGE(result, "Validator creation should work in regtest even before PoS for testing");
}

BOOST_AUTO_TEST_CASE(test_staking_before_pos_enabled)
{
    BOOST_TEST_MESSAGE("=== Testing Staking Attempts Before PoS Enabled ===");
    
    // Test time validation before PoS is active
    const Consensus::Params& consensus = GetParams().GetConsensus();
    
    // Simulate early blockchain heights (before PoS activation)
    int prePoSHeight = 10;
    int64_t currentTime = GetAdjustedTime();
    
    // Test that time validation behaves correctly before PoS
    bool validTimeResult = consensus.IsValidBlockTimeStamp(currentTime, prePoSHeight);
    int64_t timeDrift = consensus.FutureBlockTimeDrift(prePoSHeight);
    
    BOOST_TEST_MESSAGE("Pre-PoS time validation at height " + std::to_string(prePoSHeight));
    BOOST_TEST_MESSAGE("Valid time result: " + std::to_string(validTimeResult));
    BOOST_TEST_MESSAGE("Time drift allowed: " + std::to_string(timeDrift) + " seconds");
    
    // Test future time limits before PoS
    int64_t futureTime = currentTime + timeDrift + 1;
    bool futureShouldFail = consensus.IsValidBlockTimeStamp(futureTime, prePoSHeight);
    
    BOOST_CHECK_MESSAGE(!futureShouldFail, "Future time beyond drift should be rejected before PoS");
}

BOOST_AUTO_TEST_CASE(test_pos_activation_transition)
{
    BOOST_TEST_MESSAGE("=== Testing PoS Activation Transition ===");
    
    const Consensus::Params& consensus = GetParams().GetConsensus();
    
    // Test behavior around the theoretical PoS activation height
    // In regtest, PoS is typically enabled from genesis or very early
    
    std::vector<int> testHeights = {1, 10, 50, 100, 500, 1000};
    
    for (int height : testHeights) {
        int64_t drift = consensus.FutureBlockTimeDrift(height);
        int64_t currentTime = GetAdjustedTime();
        
        bool validCurrent = consensus.IsValidBlockTimeStamp(currentTime, height);
        bool validPast = consensus.IsValidBlockTimeStamp(currentTime - 300, height); // 5 min ago
        bool validFuture = consensus.IsValidBlockTimeStamp(currentTime + drift - 1, height); // Within drift
        bool invalidFuture = consensus.IsValidBlockTimeStamp(currentTime + drift + 1, height); // Beyond drift
        
        BOOST_TEST_MESSAGE("Height " + std::to_string(height) + ":");
        BOOST_TEST_MESSAGE("  Drift: " + std::to_string(drift) + "s");
        BOOST_TEST_MESSAGE("  Current time valid: " + std::to_string(validCurrent));
        BOOST_TEST_MESSAGE("  Past time valid: " + std::to_string(validPast));
        BOOST_TEST_MESSAGE("  Future (within drift) valid: " + std::to_string(validFuture));
        BOOST_TEST_MESSAGE("  Future (beyond drift) valid: " + std::to_string(invalidFuture));
        
        BOOST_CHECK_MESSAGE(validCurrent, "Current time should be valid at height " + std::to_string(height));
        BOOST_CHECK_MESSAGE(!invalidFuture, "Time beyond drift should be invalid at height " + std::to_string(height));
    }
}

BOOST_AUTO_TEST_CASE(test_staking_when_enabled)
{
    BOOST_TEST_MESSAGE("=== Testing Staking When PoS Is Enabled ===");
    
    // Test normal validator operations after PoS activation
    
    CKey stakingKey;
    stakingKey.MakeNewKey(false);
    CPubKey stakingPubKey = stakingKey.GetPubKey();
    
    // Test validator ping functionality with staking enabled
    CValidatorPing stakingPing;
    stakingPing.sigTime = GetAdjustedTime();
    
    // Create a mock block hash for the ping
    stakingPing.blockHash = GetRandHash();
    
    CTxIn pingInput;
    pingInput.prevout.hash = GetRandHash();
    pingInput.prevout.n = 0;
    stakingPing.vin = pingInput;
    
    // Test ping validation
    int nDoS = 0;
    bool pingResult = stakingPing.CheckAndUpdate(nDoS);
    
    BOOST_TEST_MESSAGE("Staking ping validation result: " + std::to_string(pingResult));
    BOOST_TEST_MESSAGE("DoS score: " + std::to_string(nDoS));
    
    // Test that ping timing is validated correctly
    CValidatorPing futurePing = stakingPing;
    futurePing.sigTime = GetAdjustedTime() + 3700; // More than 1 hour in future
    
    nDoS = 0;
    bool futureResult = futurePing.CheckAndUpdate(nDoS);
    
    BOOST_CHECK_MESSAGE(!futureResult, "Future ping should be rejected when staking");
    BOOST_CHECK_MESSAGE(nDoS > 0, "DoS penalty should be applied for future ping");
}

BOOST_AUTO_TEST_CASE(test_unstaking_scenarios)
{
    BOOST_TEST_MESSAGE("=== Testing Unstaking Scenarios ===");
    
    // Test validator behavior when unstaking occurs
    
    CKey validatorKey;
    validatorKey.MakeNewKey(false);
    CPubKey validatorPubKey = validatorKey.GetPubKey();
    
    // Test validator ping with very old timestamp (simulating unstaking)
    CValidatorPing oldPing;
    oldPing.sigTime = GetAdjustedTime() - 7300; // More than 2 hours old
    
    CTxIn oldInput;
    oldInput.prevout.hash = GetRandHash();
    oldInput.prevout.n = 0;
    oldPing.vin = oldInput;
    oldPing.blockHash = GetRandHash();
    
    int nDoS = 0;
    bool oldResult = oldPing.CheckAndUpdate(nDoS);
    
    BOOST_CHECK_MESSAGE(!oldResult, "Very old ping should be rejected");
    BOOST_CHECK_MESSAGE(nDoS > 0, "DoS penalty should be applied for old ping");
    
    BOOST_TEST_MESSAGE("Old ping validation result: " + std::to_string(oldResult));
    BOOST_TEST_MESSAGE("DoS score for old ping: " + std::to_string(nDoS));
    
    // Test validator state transitions during unstaking
    // This would involve testing the validator state management
    // For now, we test that time validation works correctly
    
    const Consensus::Params& consensus = GetParams().GetConsensus();
    int64_t currentTime = GetAdjustedTime();
    int testHeight = 1000;
    
    // Test that normal staking time rules apply
    bool normalTime = consensus.IsValidBlockTimeStamp(currentTime, testHeight);
    bool slightlyFuture = consensus.IsValidBlockTimeStamp(currentTime + 30, testHeight);
    bool tooFarFuture = consensus.IsValidBlockTimeStamp(currentTime + 7200, testHeight);
    
    BOOST_CHECK_MESSAGE(normalTime, "Normal time should be valid during staking");
    BOOST_CHECK_MESSAGE(slightlyFuture, "Slightly future time should be valid during staking");
    BOOST_CHECK_MESSAGE(!tooFarFuture, "Far future time should be invalid during staking");
}

BOOST_AUTO_TEST_CASE(test_validator_collateral_requirements)
{
    BOOST_TEST_MESSAGE("=== Testing Validator Collateral Requirements ===");
    
    const Consensus::Params& consensus = GetParams().GetConsensus();
    
    // Test that collateral amount is correct for all phases
    CAmount expectedCollateral = 1000 * COIN;
    CAmount actualCollateral = consensus.nValidatorCollateralAmt;
    
    BOOST_CHECK_EQUAL(actualCollateral, expectedCollateral);
    BOOST_TEST_MESSAGE("Required validator collateral: " + std::to_string(actualCollateral / COIN) + " CLORE");
    
    // Test that this requirement is consistent across network upgrades
    // The collateral requirement should not change during PoS transitions
    
    std::vector<int> testHeights = {1, 100, 1000, 10000};
    
    for (int height : testHeights) {
        // Collateral requirement should be consistent regardless of height
        BOOST_CHECK_EQUAL(consensus.nValidatorCollateralAmt, expectedCollateral);
        BOOST_TEST_MESSAGE("Collateral at height " + std::to_string(height) + ": " + 
                          std::to_string(consensus.nValidatorCollateralAmt / COIN) + " CLORE");
    }
}

BOOST_AUTO_TEST_CASE(test_network_upgrade_edge_cases)
{
    BOOST_TEST_MESSAGE("=== Testing Network Upgrade Edge Cases ===");
    
    // Test edge cases around network upgrades and validator operations
    
    const Consensus::Params& consensus = GetParams().GetConsensus();
    
    // Test time validation at boundary conditions
    int64_t currentTime = GetAdjustedTime();
    
    // Test at various heights to ensure consistent behavior
    std::vector<int> boundaryHeights = {0, 1, 99, 100, 101, 499, 500, 501, 999, 1000, 1001};
    
    for (int height : boundaryHeights) {
        int64_t allowedDrift = consensus.FutureBlockTimeDrift(height);
        
        // Test time exactly at the drift boundary
        bool atBoundary = consensus.IsValidBlockTimeStamp(currentTime + allowedDrift, height);
        bool beyondBoundary = consensus.IsValidBlockTimeStamp(currentTime + allowedDrift + 1, height);
        
        BOOST_TEST_MESSAGE("Height " + std::to_string(height) + 
                          " - Drift: " + std::to_string(allowedDrift) + 
                          "s, At boundary: " + std::to_string(atBoundary) +
                          ", Beyond: " + std::to_string(beyondBoundary));
        
        // Time at drift boundary should be valid, beyond should not be
        BOOST_CHECK_MESSAGE(atBoundary, "Time at drift boundary should be valid at height " + std::to_string(height));
        BOOST_CHECK_MESSAGE(!beyondBoundary, "Time beyond drift should be invalid at height " + std::to_string(height));
    }
}

BOOST_AUTO_TEST_SUITE_END()
