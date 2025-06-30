// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_VALIDATOR_H
#define CLORE_VALIDATOR_H

#include "key.h"
#include "net.h"
#include "primitives/transaction.h"
#include "sync.h"
#include "util.h"

/* Depth of the block pinged by validators */
static const unsigned int MNPING_DEPTH = 12;
static const int MIN_BIP155_PROTOCOL_VERSION = 70016;

// Forward declarations
class CValidator;
class CValidatorBroadcast;
class CValidatorPing;

typedef std::shared_ptr<CValidator> ValidatorRef;

// Simple validator state enum
enum validator_state_enum {
    VALIDATOR_ENABLED = 1,
    VALIDATOR_PRE_ENABLED = 2,
    VALIDATOR_EXPIRED = 3,
    VALIDATOR_REMOVE = 4,
    VALIDATOR_VIN_SPENT = 5
};

// Simple ping class for basic functionality
class CValidatorPing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime;

    CValidatorPing() : sigTime(0) {}
    CValidatorPing(const CTxIn& newVin, const uint256& nBlockHash, uint64_t _sigTime)
        : vin(newVin), blockHash(nBlockHash), sigTime(_sigTime) {}

    bool IsNull() const { return blockHash.IsNull() || vin.prevout.IsNull(); }
    uint256 GetHash() const { return uint256(); }                                                                  // Stub
    std::string GetStrMessage() const { return ""; }                                                               // Stub
    bool CheckAndUpdate(int& nDos, bool fRequireAvailable = true, bool fCheckSigTimeOnly = false) { return true; } // Stub
    void Relay() {}                                                                                                // Stub
};

// Simple validator class for basic functionality
class CValidator
{
public:
    enum state {
        VALIDATOR_PRE_ENABLED,
        VALIDATOR_ENABLED,
        VALIDATOR_EXPIRED,
        VALIDATOR_REMOVE,
        VALIDATOR_VIN_SPENT,
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyValidator;
    int64_t sigTime;
    int protocolVersion;
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CValidatorPing lastPing;
    int nActiveState;

    CValidator()
        : sigTime(0), protocolVersion(PROTOCOL_VERSION),
          nScanningErrorCount(0), nLastScanningErrorBlockHeight(0),
          nActiveState(VALIDATOR_ENABLED) {}

    CValidator(const CValidator& other) = default;
    CValidator& operator=(const CValidator& other) = default;

    bool IsEnabled() const { return nActiveState == VALIDATOR_ENABLED; }
    bool IsPreEnabled() const { return nActiveState == VALIDATOR_PRE_ENABLED; }
    bool IsAvailableState() const { return IsEnabled() || IsPreEnabled(); }

    std::string Status() const
    {
        switch (nActiveState) {
        case VALIDATOR_PRE_ENABLED:
            return "PRE_ENABLED";
        case VALIDATOR_ENABLED:
            return "ENABLED";
        case VALIDATOR_EXPIRED:
            return "EXPIRED";
        case VALIDATOR_VIN_SPENT:
            return "VIN_SPENT";
        case VALIDATOR_REMOVE:
            return "REMOVE";
        default:
            return "UNKNOWN";
        }
    }

    CValidator::state GetActiveState() const
    {
        switch (nActiveState) {
        case VALIDATOR_PRE_ENABLED:
            return VALIDATOR_PRE_ENABLED;
        case VALIDATOR_ENABLED:
            return VALIDATOR_ENABLED;
        case VALIDATOR_EXPIRED:
            return VALIDATOR_EXPIRED;
        case VALIDATOR_VIN_SPENT:
            return VALIDATOR_VIN_SPENT;
        case VALIDATOR_REMOVE:
            return VALIDATOR_REMOVE;
        default:
            return VALIDATOR_ENABLED;
        }
    }

    bool IsBroadcastedWithin(int seconds) { return (GetAdjustedTime() - sigTime) < seconds; }
    bool IsPingedWithin(int seconds, int64_t now = -1) const
    {
        now = (now == -1) ? GetAdjustedTime() : now;
        return lastPing.IsNull() ? false : now - lastPing.sigTime < seconds;
    }

    void SetLastPing(const CValidatorPing& _lastPing) { lastPing = _lastPing; }

    // Static helper functions
    static bool IsValidStateForAutoStart(int nActiveStateIn)
    {
        return nActiveStateIn == VALIDATOR_ENABLED || nActiveStateIn == VALIDATOR_PRE_ENABLED;
    }

    static std::string StateToString(int nStateIn)
    {
        switch (nStateIn) {
        case VALIDATOR_PRE_ENABLED:
            return "PRE_ENABLED";
        case VALIDATOR_ENABLED:
            return "ENABLED";
        case VALIDATOR_EXPIRED:
            return "EXPIRED";
        case VALIDATOR_VIN_SPENT:
            return "VIN_SPENT";
        case VALIDATOR_REMOVE:
            return "REMOVE";
        default:
            return "UNKNOWN";
        }
    }

    // Stub methods for compatibility
    bool UpdateFromNewBroadcast(CValidatorBroadcast& mnb) { return true; }
    void Check() {}                              // Stub
    bool IsValidNetAddr() const { return true; } // Stub
};

// Simple broadcast class
class CValidatorBroadcast : public CValidator
{
public:
    CValidatorBroadcast() = default;
    CValidatorBroadcast(const CValidator& validator) : CValidator(validator) {}

    bool CheckAndUpdate(int& nDoS) { return true; }                    // Stub
    uint256 GetHash() const { return uint256(); }                      // Stub
    void Relay() {}                                                    // Stub
    bool Sign(const CKey& key, const CPubKey& pubKey) { return true; } // Stub
    bool CheckSignature() const { return true; }                       // Stub

    static bool Create(const CTxIn& vin, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyValidatorNew, const CPubKey& pubKeyValidatorNew, std::string& strErrorRet, CValidatorBroadcast& mnbRet) { return true; } // Stub

    static bool CheckDefaultPort(CService service, std::string& strErrorRet, const std::string& strContext)
    {
        return true; // Stub
    }
};

// Global variables that may be needed
extern bool fValidatorMode;

#endif // CLORE_VALIDATOR_H