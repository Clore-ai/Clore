// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_MASTERNODE_H
#define CLORE_MASTERNODE_H

#include "key.h"
#include "net.h"
#include "primitives/transaction.h"
#include "sync.h"
#include "util.h"

/* Depth of the block pinged by masternodes */
static const unsigned int MNPING_DEPTH = 12;
static const int MIN_BIP155_PROTOCOL_VERSION = 70016;

// Forward declarations
class CMasternode;
class CMasternodeBroadcast;
class CMasternodePing;

typedef std::shared_ptr<CMasternode> MasternodeRef;

// Simple masternode state enum
enum masternode_state_enum {
    MASTERNODE_ENABLED = 1,
    MASTERNODE_PRE_ENABLED = 2,
    MASTERNODE_EXPIRED = 3,
    MASTERNODE_REMOVE = 4,
    MASTERNODE_VIN_SPENT = 5
};

// Simple ping class for basic functionality
class CMasternodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime;

    CMasternodePing() : sigTime(0) {}
    CMasternodePing(const CTxIn& newVin, const uint256& nBlockHash, uint64_t _sigTime)
        : vin(newVin), blockHash(nBlockHash), sigTime(_sigTime) {}

    bool IsNull() const { return blockHash.IsNull() || vin.prevout.IsNull(); }
    uint256 GetHash() const { return uint256(); }                                                                  // Stub
    std::string GetStrMessage() const { return ""; }                                                               // Stub
    bool CheckAndUpdate(int& nDos, bool fRequireAvailable = true, bool fCheckSigTimeOnly = false) { return true; } // Stub
    void Relay() {}                                                                                                // Stub
};

// Simple masternode class for basic functionality
class CMasternode
{
public:
    enum state {
        MASTERNODE_PRE_ENABLED,
        MASTERNODE_ENABLED,
        MASTERNODE_EXPIRED,
        MASTERNODE_REMOVE,
        MASTERNODE_VIN_SPENT,
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyMasternode;
    int64_t sigTime;
    int protocolVersion;
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CMasternodePing lastPing;
    int nActiveState;

    CMasternode()
        : sigTime(0), protocolVersion(PROTOCOL_VERSION),
          nScanningErrorCount(0), nLastScanningErrorBlockHeight(0),
          nActiveState(MASTERNODE_ENABLED) {}

    CMasternode(const CMasternode& other) = default;
    CMasternode& operator=(const CMasternode& other) = default;

    bool IsEnabled() const { return nActiveState == MASTERNODE_ENABLED; }
    bool IsPreEnabled() const { return nActiveState == MASTERNODE_PRE_ENABLED; }
    bool IsAvailableState() const { return IsEnabled() || IsPreEnabled(); }

    std::string Status() const
    {
        switch (nActiveState) {
        case MASTERNODE_PRE_ENABLED:
            return "PRE_ENABLED";
        case MASTERNODE_ENABLED:
            return "ENABLED";
        case MASTERNODE_EXPIRED:
            return "EXPIRED";
        case MASTERNODE_VIN_SPENT:
            return "VIN_SPENT";
        case MASTERNODE_REMOVE:
            return "REMOVE";
        default:
            return "UNKNOWN";
        }
    }

    CMasternode::state GetActiveState() const
    {
        switch (nActiveState) {
        case MASTERNODE_PRE_ENABLED:
            return MASTERNODE_PRE_ENABLED;
        case MASTERNODE_ENABLED:
            return MASTERNODE_ENABLED;
        case MASTERNODE_EXPIRED:
            return MASTERNODE_EXPIRED;
        case MASTERNODE_VIN_SPENT:
            return MASTERNODE_VIN_SPENT;
        case MASTERNODE_REMOVE:
            return MASTERNODE_REMOVE;
        default:
            return MASTERNODE_ENABLED;
        }
    }

    bool IsBroadcastedWithin(int seconds) { return (GetAdjustedTime() - sigTime) < seconds; }
    bool IsPingedWithin(int seconds, int64_t now = -1) const
    {
        now = (now == -1) ? GetAdjustedTime() : now;
        return lastPing.IsNull() ? false : now - lastPing.sigTime < seconds;
    }

    void SetLastPing(const CMasternodePing& _lastPing) { lastPing = _lastPing; }

    // Static helper functions
    static bool IsValidStateForAutoStart(int nActiveStateIn)
    {
        return nActiveStateIn == MASTERNODE_ENABLED || nActiveStateIn == MASTERNODE_PRE_ENABLED;
    }

    static std::string StateToString(int nStateIn)
    {
        switch (nStateIn) {
        case MASTERNODE_PRE_ENABLED:
            return "PRE_ENABLED";
        case MASTERNODE_ENABLED:
            return "ENABLED";
        case MASTERNODE_EXPIRED:
            return "EXPIRED";
        case MASTERNODE_VIN_SPENT:
            return "VIN_SPENT";
        case MASTERNODE_REMOVE:
            return "REMOVE";
        default:
            return "UNKNOWN";
        }
    }

    // Stub methods for compatibility
    bool UpdateFromNewBroadcast(CMasternodeBroadcast& mnb) { return true; }
    void Check() {}                              // Stub
    bool IsValidNetAddr() const { return true; } // Stub
};

// Simple broadcast class
class CMasternodeBroadcast : public CMasternode
{
public:
    CMasternodeBroadcast() = default;
    CMasternodeBroadcast(const CMasternode& mn) : CMasternode(mn) {}

    bool CheckAndUpdate(int& nDoS) { return true; }                    // Stub
    uint256 GetHash() const { return uint256(); }                      // Stub
    void Relay() {}                                                    // Stub
    bool Sign(const CKey& key, const CPubKey& pubKey) { return true; } // Stub
    bool CheckSignature() const { return true; }                       // Stub

    static bool Create(const CTxIn& vin, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyMasternodeNew, const CPubKey& pubKeyMasternodeNew, std::string& strErrorRet, CMasternodeBroadcast& mnbRet) { return true; } // Stub

    static bool CheckDefaultPort(CService service, std::string& strErrorRet, const std::string& strContext)
    {
        return true; // Stub
    }
};

// Global variables that may be needed
extern bool fMasternodeMode;

#endif // CLORE_MASTERNODE_H