// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"
#include "chainparams.h"
#include "net.h"
#include "util.h"
#include "validation.h"

// Global variables
bool fMasternodeMode = false;

// Simple masternode sync stub
class CMasternodeSync
{
public:
    bool IsBlockchainSynced() { return !IsInitialBlockDownload(); }
    bool IsSynced() { return !IsInitialBlockDownload(); }
    void AddedMasternodeList(const uint256& hash) { /* stub */ }
};
CMasternodeSync masternodeSync;

// Missing constants
static const int MIN_MASTERNODE_PAYMENT_PROTO_VERSION __attribute__((unused)) = 70000;
static const int nMasternodeMinimumConfirmations __attribute__((unused)) = 15;

// Missing protocol functions
int ActiveProtocol() { return PROTOCOL_VERSION; }

#define MASTERNODE_MIN_MNP_SECONDS_REGTEST 90
#define MASTERNODE_MIN_MNB_SECONDS_REGTEST 25
#define MASTERNODE_PING_SECONDS_REGTEST 25
#define MASTERNODE_EXPIRATION_SECONDS_REGTEST 12 * 60
#define MASTERNODE_REMOVAL_SECONDS_REGTEST 13 * 60

#define MASTERNODE_MIN_MNP_SECONDS (10 * 60)
#define MASTERNODE_MIN_MNB_SECONDS (5 * 60)
#define MASTERNODE_PING_SECONDS (5 * 60)
#define MASTERNODE_EXPIRATION_SECONDS (120 * 60)
#define MASTERNODE_REMOVAL_SECONDS (130 * 60)
#define MASTERNODE_CHECK_SECONDS 5

int MasternodeMinPingSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? MASTERNODE_MIN_MNP_SECONDS_REGTEST : MASTERNODE_MIN_MNP_SECONDS;
}

int MasternodeBroadcastSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? MASTERNODE_MIN_MNB_SECONDS_REGTEST : MASTERNODE_MIN_MNB_SECONDS;
}

int MasternodePingSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? MASTERNODE_PING_SECONDS_REGTEST : MASTERNODE_PING_SECONDS;
}

int MasternodeExpirationSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? MASTERNODE_EXPIRATION_SECONDS_REGTEST : MASTERNODE_EXPIRATION_SECONDS;
}

int MasternodeRemovalSeconds()
{
    return GetParams().NetworkIDString() == "regtest" ? MASTERNODE_REMOVAL_SECONDS_REGTEST : MASTERNODE_REMOVAL_SECONDS;
}