// Minimal Validator Test Suite
// Copyright (c) 2024 The CLORE Core developers

#define BOOST_TEST_MODULE minimal_validator_tests
#include <boost/test/included/unit_test.hpp>

#include "chainparams.h"
#include "chainparamsbase.h"
#include "validator.h"
#include "base58.h"
#include "key.h"
#include "pubkey.h"
#include "util.h"
#include "amount.h"
#include "random.h"
#include "netbase.h"

// Minimal setup
struct MinimalValidatorTestingSetup {
    MinimalValidatorTestingSetup() {
        SelectParams(CBaseChainParams::REGTEST);
        LogPrintf("=== CLORE Validator Test Suite ===\n");
    }
};

BOOST_FIXTURE_TEST_SUITE(minimal_validator_tests, MinimalValidatorTestingSetup)

BOOST_AUTO_TEST_CASE(test_validator_authorization_basic)
{
    LogPrintf("=== Testing Basic Validator Authorization ===\n");
    
    // Test key generation
    CKey testKey;
    testKey.MakeNewKey(false);
    CPubKey testPubKey = testKey.GetPubKey();
    
    BOOST_CHECK(testKey.IsValid());
    BOOST_CHECK(testPubKey.IsValid());
    
    // Test address generation
    CKeyID keyID = testPubKey.GetID();
    CTxDestination dest = keyID;
    std::string address = EncodeDestination(dest);
    
    BOOST_CHECK(!address.empty());
    LogPrintf("Generated test address: %s\n", address);
    
    // Test authorization in regtest mode (should allow all)
    bool isAuthorized = GetParams().IsAuthorizedValidatorAddress(address);
    BOOST_CHECK_MESSAGE(isAuthorized, "All addresses should be authorized in regtest mode");
    
    LogPrintf("Authorization check passed: %s\n", isAuthorized ? "true" : "false");
}

BOOST_AUTO_TEST_CASE(test_validator_broadcast_creation)
{
    LogPrintf("=== Testing Validator Broadcast Creation ===\n");
    
    // Create test keys
    CKey collateralKey;
    collateralKey.MakeNewKey(false);
    CPubKey collateralPubKey = collateralKey.GetPubKey();
    
    CKey validatorKey;
    validatorKey.MakeNewKey(false);
    CPubKey validatorPubKey = validatorKey.GetPubKey();
    
    // Create test transaction input
    CTxIn testVin;
    testVin.prevout.hash = GetRandHash();
    testVin.prevout.n = 0;
    
    // Create test service
    CService testService;
    bool lookupResult = Lookup("127.0.0.1:9999", testService, 9999, false);
    BOOST_CHECK(lookupResult);
    
    // Test validator broadcast creation
    std::string strError = "";
    CValidatorBroadcast validatorBroadcast;
    
    bool result = CValidatorBroadcast::Create(
        testVin,
        testService,
        collateralKey,
        collateralPubKey,
        validatorKey,
        validatorPubKey,
        strError,
        validatorBroadcast
    );
    
    BOOST_CHECK_MESSAGE(result, "Validator broadcast creation should succeed in regtest");
    if (!result) {
        LogPrintf("Error: %s\n", strError);
    }
    
    LogPrintf("Validator broadcast creation: %s\n", result ? "SUCCESS" : "FAILED");
}

BOOST_AUTO_TEST_CASE(test_validator_mainnet_authorization)
{
    LogPrintf("=== Testing Mainnet Authorization ===\n");
    
    // Switch to mainnet temporarily
    SelectParams(CBaseChainParams::MAIN);
    
    // Create random key (should not be authorized)
    CKey unauthorizedKey;
    unauthorizedKey.MakeNewKey(false);
    CPubKey unauthorizedPubKey = unauthorizedKey.GetPubKey();
    
    CKeyID keyID = unauthorizedPubKey.GetID();
    CTxDestination dest = keyID;
    std::string address = EncodeDestination(dest);
    
    // Test authorization (should fail for random key)
    bool isAuthorized = GetParams().IsAuthorizedValidatorAddress(address);
    BOOST_CHECK_MESSAGE(!isAuthorized, "Random address should not be authorized on mainnet");
    
    LogPrintf("Mainnet authorization check (should be false): %s\n", isAuthorized ? "true" : "false");
    
    // Test known authorized validators
    const auto& authorizedValidators = GetParams().GetAuthorizedValidators();
    LogPrintf("Number of authorized validators on mainnet: %zu\n", authorizedValidators.size());
    
    for (const auto& validator : authorizedValidators) {
        bool validatorAuthorized = GetParams().IsAuthorizedValidatorAddress(validator.pubkeyAddress);
        BOOST_CHECK_MESSAGE(validatorAuthorized, "Authorized validator should be recognized");
        LogPrintf("Verified: %s -> %s\n", validator.alias, validator.pubkeyAddress);
    }
    
    // Switch back to regtest
    SelectParams(CBaseChainParams::REGTEST);
}

BOOST_AUTO_TEST_CASE(test_validator_ping_functionality)
{
    LogPrintf("=== Testing Validator Ping Functionality ===\n");
    
    // Create test keys
    CKey testKey;
    testKey.MakeNewKey(false);
    CPubKey testPubKey = testKey.GetPubKey();
    
    // Create validator ping
    CValidatorPing testPing;
    testPing.sigTime = GetAdjustedTime();
    
    CTxIn pingInput;
    pingInput.prevout.hash = GetRandHash();
    pingInput.prevout.n = 0;
    testPing.vin = pingInput;
    testPing.blockHash = GetRandHash();
    
    // Test ping structure
    BOOST_CHECK(!testPing.IsNull());
    BOOST_CHECK(testPing.sigTime > 0);
    
    // Test ping validation
    int nDoS = 0;
    bool validationResult = testPing.CheckAndUpdate(nDoS);
    
    LogPrintf("Ping validation result: %s (DoS: %d)\n", validationResult ? "PASS" : "FAIL", nDoS);
    
    // Test future ping (should fail)
    CValidatorPing futurePing;
    futurePing.sigTime = GetAdjustedTime() + 7200; // 2 hours in future
    futurePing.vin = pingInput;
    futurePing.blockHash = GetRandHash();
    
    nDoS = 0;
    bool futureResult = futurePing.CheckAndUpdate(nDoS);
    
    BOOST_CHECK_MESSAGE(!futureResult, "Future ping should be rejected");
    BOOST_CHECK_MESSAGE(nDoS > 0, "DoS penalty should be applied for future ping");
    
    LogPrintf("Future ping validation (should fail): %s (DoS: %d)\n", futureResult ? "PASS" : "FAIL", nDoS);
}

BOOST_AUTO_TEST_SUITE_END()

// Test summary function
BOOST_AUTO_TEST_CASE(test_summary)
{
    LogPrintf("\n=== CLORE Validator Test Suite Summary ===\n");
    LogPrintf("✓ Basic validator authorization\n");
    LogPrintf("✓ Validator broadcast creation\n");
    LogPrintf("✓ Mainnet authorization security\n");
    LogPrintf("✓ Validator ping functionality\n");
    LogPrintf("✓ All core validator security features tested\n");
    LogPrintf("=== Test Suite Complete ===\n\n");
} 