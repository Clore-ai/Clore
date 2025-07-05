// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Validator Authorization Tests - EXTREMELY CRITICAL
 * 
 * Tests validator authorization system ensuring only authorized validators can participate in PoS consensus during the PoW→PoS transition.
 * Essential security component preventing unauthorized nodes from becoming validators and maintaining decentralized validator network integrity.
 * 
 * IMPACT SUMMARY:
 * - Security: Validates authorization system preventing unauthorized validator participation, protecting PoS consensus integrity
 * - Performance: Ensures efficient validator verification during network consensus operations and broadcast validation
 * - Users: Protects network security ensuring only legitimate validators can process transactions and secure the blockchain
 * - Business: Critical for maintaining validator network integrity, preventing attacks, and ensuring reliable PoS transition
 */

#include "test/test_clore.h"
#include "chainparams.h"
#include "validator.h"
#include "base58.h"
#include "key.h"
#include "pubkey.h"
#include "util.h"
#include "netbase.h"

#include <boost/test/unit_test.hpp>

struct ValidatorAuthTestingSetup : public TestingSetup {
    ValidatorAuthTestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(validator_authorization_tests, ValidatorAuthTestingSetup)

BOOST_AUTO_TEST_CASE(test_authorized_validator_broadcast_creation)
{
    // Test that an authorized validator can create broadcasts successfully in regtest
    
    CKey testKey;
    testKey.MakeNewKey(false);
    CPubKey testPubKey = testKey.GetPubKey();
    
    CTxIn testVin;
    testVin.prevout.hash = GetRandHash();
    testVin.prevout.n = 0;
    
    CService testService;
    BOOST_CHECK(Lookup("127.0.0.1:9999", testService, 9999, false));
    
    CKey testValidatorKey;
    testValidatorKey.MakeNewKey(false);
    CPubKey testValidatorPubKey = testValidatorKey.GetPubKey();
    
    std::string strError = "";
    CValidatorBroadcast testBroadcast;
    
    // In regtest mode, this should succeed for any valid keys
    bool result = CValidatorBroadcast::Create(
        testVin,
        testService, 
        testKey,
        testPubKey,
        testValidatorKey,
        testValidatorPubKey,
        strError,
        testBroadcast
    );
    
    BOOST_TEST_MESSAGE("Authorized validator broadcast creation result: " + std::to_string(result));
    BOOST_TEST_MESSAGE("Error message (if any): " + strError);
    
    // In regtest mode, this should always succeed
    BOOST_CHECK_MESSAGE(result, "Authorized validator should be able to create broadcast in regtest mode");
    BOOST_CHECK_MESSAGE(strError.empty(), "No error should occur for authorized validator: " + strError);
}

BOOST_AUTO_TEST_CASE(test_unauthorized_validator_broadcast_creation_mainnet)
{
    // Test that an unauthorized validator cannot create broadcasts on mainnet
    
    SelectParams(CBaseChainParams::MAIN);
    
    CKey unauthorizedKey;
    unauthorizedKey.MakeNewKey(false);
    CPubKey unauthorizedPubKey = unauthorizedKey.GetPubKey();
    
    CKeyID keyID = unauthorizedPubKey.GetID();
    CTxDestination dest = keyID;
    std::string address = EncodeDestination(dest);
    
    // Verify this address is not authorized
    BOOST_CHECK_MESSAGE(!GetParams().IsAuthorizedValidatorAddress(address), 
                        "Test key should not be in authorized list: " + address);
    
    CTxIn testVin;
    testVin.prevout.hash = GetRandHash();
    testVin.prevout.n = 0;
    
    CService testService;
    BOOST_CHECK(Lookup("127.0.0.1:8788", testService, 8788, false));
    
    CKey testValidatorKey;
    testValidatorKey.MakeNewKey(false);
    CPubKey testValidatorPubKey = testValidatorKey.GetPubKey();
    
    std::string strError = "";
    CValidatorBroadcast testBroadcast;
    
    bool result = CValidatorBroadcast::Create(
        testVin,
        testService,
        unauthorizedKey,
        unauthorizedPubKey,
        testValidatorKey,
        testValidatorPubKey,
        strError,
        testBroadcast
    );
    
    BOOST_TEST_MESSAGE("Unauthorized validator broadcast creation result: " + std::to_string(result));
    BOOST_TEST_MESSAGE("Error message: " + strError);
    BOOST_TEST_MESSAGE("Unauthorized address: " + address);
    
    // This should fail for unauthorized validators on mainnet
    BOOST_CHECK_MESSAGE(!result, "Unauthorized validator should NOT be able to create broadcast on mainnet");
    BOOST_CHECK_MESSAGE(!strError.empty(), "Error message should be provided for unauthorized validator");
    BOOST_CHECK_MESSAGE(strError.find("not authorized") != std::string::npos, 
                        "Error should mention authorization failure: " + strError);
    
    SelectParams(CBaseChainParams::REGTEST);
}

BOOST_AUTO_TEST_SUITE_END()
