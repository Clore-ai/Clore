// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2021 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_ACTIVEVALIDATOR_H
#define CLORE_ACTIVEVALIDATOR_H

#include "key.h"
#include "net.h"
#include "primitives/transaction.h" // for COutPoint, CTxIn
#include "sync.h"
#include "util.h"

// Validator state definitions
#define ACTIVE_VALIDATOR_INITIAL 0 // initial state
#define ACTIVE_VALIDATOR_SYNC_IN_PROCESS 1
#define ACTIVE_VALIDATOR_NOT_CAPABLE 3
#define ACTIVE_VALIDATOR_STARTED 4
#define ACTIVE_VALIDATOR_INPUT_TOO_NEW 5

class CActiveValidator
{
private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;

    // Validator state
    int nState{ACTIVE_VALIDATOR_INITIAL};
    std::string strNotCapableReason;

    // Validator type
    enum validator_type_t {
        VALIDATOR_UNKNOWN = 0,
        VALIDATOR_REMOTE = 1,
        VALIDATOR_LOCAL = 2
    } eType{VALIDATOR_UNKNOWN};

    // Ping service
    bool fPingerEnabled{false};

    // Sentinel ping data
    int64_t nSentinelPingTime{0};
    uint32_t nSentinelVersion{0};

    // Internal state management
    void ManageStateInitial();
    void ManageStateRemote();
    void ManageStateLocal();

public:
    CActiveValidator() = default;

    // Validator info
    COutPoint outpoint;
    CService service;
    CPubKey pubKeyValidator;
    CKey keyValidator;

    // State management
    void ManageState();
    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

    // Ping
    bool SendValidatorPing();
    bool UpdateSentinelPing(int version);

    // Accessors
    int GetState() const { return nState; }
    validator_type_t GetType() const { return eType; }
    bool IsPingerEnabled() const { return fPingerEnabled; }
    void SetPingerEnabled(bool enabled) { fPingerEnabled = enabled; }
    uint32_t GetSentinelVersion() const { return nSentinelVersion; }
    int64_t GetSentinelPingTime() const { return nSentinelPingTime; }
    std::string GetStatusMessage() const { return strNotCapableReason; }
};

// Global instance
extern CActiveValidator activeValidator;

#endif // CLORE_ACTIVEVALIDATOR_H