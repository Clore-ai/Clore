// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "net.h"
#include "util.h"
#include "validation.h"
#include "validator.h"

// Global variables are defined in activevalidator.cpp

// Simple validator sync stub
class CValidatorSync
{
public:
    bool IsBlockchainSynced() { return !IsInitialBlockDownload(); }
    bool IsSynced() { return !IsInitialBlockDownload(); }
    void AddedValidatorList(const uint256& hash) { /* stub */ }
};
CValidatorSync validatorSync;

// Missing constants
static const int MIN_VALIDATOR_PAYMENT_PROTO_VERSION __attribute__((unused)) = 70000;
static const int nValidatorMinimumConfirmations __attribute__((unused)) = 15;

// Missing protocol functions
int ActiveProtocol() { return PROTOCOL_VERSION; }

#define VALIDATOR_MIN_MNP_SECONDS_REGTEST 90
#define VALIDATOR_MIN_MNB_SECONDS_REGTEST 25
#define VALIDATOR_PING_SECONDS_REGTEST 25
#define VALIDATOR_EXPIRATION_SECONDS_REGTEST 12 * 60
#define VALIDATOR_REMOVAL_SECONDS_REGTEST 13 * 60

#define VALIDATOR_MIN_MNP_SECONDS (10 * 60)
#define VALIDATOR_MIN_MNB_SECONDS (5 * 60)
#define VALIDATOR_PING_SECONDS (5 * 60)
#define VALIDATOR_EXPIRATION_SECONDS (120 * 60)
#define VALIDATOR_REMOVAL_SECONDS (130 * 60)
#define VALIDATOR_CHECK_SECONDS 5

int ValidatorMinPingSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? VALIDATOR_MIN_MNP_SECONDS_REGTEST : VALIDATOR_MIN_MNP_SECONDS;
}

int ValidatorBroadcastSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? VALIDATOR_MIN_MNB_SECONDS_REGTEST : VALIDATOR_MIN_MNB_SECONDS;
}

int ValidatorPingSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? VALIDATOR_PING_SECONDS_REGTEST : VALIDATOR_PING_SECONDS;
}

int ValidatorExpirationSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? VALIDATOR_EXPIRATION_SECONDS_REGTEST : VALIDATOR_EXPIRATION_SECONDS;
}

int ValidatorRemovalSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? VALIDATOR_REMOVAL_SECONDS_REGTEST : VALIDATOR_REMOVAL_SECONDS;
}