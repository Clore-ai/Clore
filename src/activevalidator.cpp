// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activevalidator.h"
#include "addrman.h"
#include "netbase.h"
#include "protocol.h"
#include "spork.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "util.h"
#include "validator.h"
#include "validatorconfig.h"
#include "validatorman.h"

// Keep track of the active Validator
CActiveValidator activeValidator;

void CActiveValidator::ManageState()
{
    LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageState -- Start\n");
    if (!fValidatorMode) {
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageState -- Not a validator, returning\n");
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !validatorSync.IsBlockchainSynced()) {
        nState = ACTIVE_VALIDATOR_SYNC_IN_PROCESS;
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if (nState == ACTIVE_VALIDATOR_SYNC_IN_PROCESS) {
        nState = ACTIVE_VALIDATOR_INITIAL;
    }

    LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if (eType == VALIDATOR_UNKNOWN) {
        ManageStateInitial();
    }

    if (eType == VALIDATOR_REMOTE) {
        ManageStateRemote();
    } else if (eType == VALIDATOR_LOCAL) {
        // Try Remote Start first so the started local validator can be restarted without recreate validator broadcast.
        ManageStateRemote();
        if (nState != ACTIVE_VALIDATOR_STARTED)
            ManageStateLocal();
    }

    SendValidatorPing();
}

std::string CActiveValidator::GetStateString() const
{
    switch (nState) {
    case ACTIVE_VALIDATOR_INITIAL:
        return "INITIAL";
    case ACTIVE_VALIDATOR_SYNC_IN_PROCESS:
        return "SYNC_IN_PROCESS";
    case ACTIVE_VALIDATOR_INPUT_TOO_NEW:
        return "INPUT_TOO_NEW";
    case ACTIVE_VALIDATOR_NOT_CAPABLE:
        return "NOT_CAPABLE";
    case ACTIVE_VALIDATOR_STARTED:
        return "STARTED";
    default:
        return "UNKNOWN";
    }
}

std::string CActiveValidator::GetStatus() const
{
    switch (nState) {
    case ACTIVE_VALIDATOR_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_VALIDATOR_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Validator";
    case ACTIVE_VALIDATOR_INPUT_TOO_NEW:
        return strprintf("Validator input must have at least %d confirmations", nValidatorMinimumConfirmations);
    case ACTIVE_VALIDATOR_NOT_CAPABLE:
        return "Not capable validator: " + strNotCapableReason;
    case ACTIVE_VALIDATOR_STARTED:
        return "Validator successfully started";
    default:
        return "Unknown";
    }
}

std::string CActiveValidator::GetTypeString() const
{
    std::string strType;
    switch (eType) {
    case VALIDATOR_REMOTE:
        strType = "REMOTE";
        break;
    case VALIDATOR_LOCAL:
        strType = "LOCAL";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveValidator::SendValidatorPing()
{
    if (!fPingerEnabled) {
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::SendValidatorPing -- %s: validator ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if (!mnodeman.Has(outpoint)) {
        strNotCapableReason = "Validator not in validator list";
        nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::SendValidatorPing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CValidatorPing mnp(outpoint);
    if (!mnp.Sign(keyValidator, pubKeyValidator)) {
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::SendValidatorPing -- ERROR: Couldn't sign Validator Ping\n");
        return false;
    }

    // Update lastPing for our validator in Validator list
    if (mnodeman.IsValidatorPingedWithin(outpoint, VALIDATOR_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::SendValidatorPing -- Too early to send Validator Ping\n");
        return false;
    }

    mnodeman.SetLastPing(outpoint, mnp);

    LogPrint(BCLog::VALIDATOR, "CActiveValidator::SendValidatorPing -- Relaying ping, collateral=%s\n", outpoint.ToStringShort());
    mnp.Relay();

    return true;
}

bool CActiveValidator::UpdateSentinelPing(int version)
{
    nSentinelVersion = version;
    return SendValidatorPing();
}

void CActiveValidator::ManageStateInitial()
{
    LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
        strNotCapableReason = "Validator must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes) {
            if (pnode->addr == service) {
                fFoundLocal = true;
                LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- found local connection\n");
                break;
            }
        }
    }

    if (!fFoundLocal) {
        nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != 9999) {
            nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
            strNotCapableReason = "Invalid port: " + std::to_string(service.GetPort()) + " - only 9999 is supported on mainnet.";
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if (service.GetPort() == 9999) {
        nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
        strNotCapableReason = "Invalid port: " + std::to_string(service.GetPort()) + " - 9999 is only supported on mainnet.";
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());

    if (!ConnectNode((CAddress)service, NULL, true)) {
        nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = VALIDATOR_REMOTE;

    // Check if wallet funds are available
    if (!pwalletMain) {
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if (pwalletMain->IsLocked()) {
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if (pwalletMain->GetBalance() < 1000 * COIN) {
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: Wallet balance is < 1000 CLORE\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetValidatorOutpointAndKeys(outpoint, pubKeyCollateral, keyCollateral)) {
        if (GetInputAge(outpoint) < nValidatorMinimumConfirmations) {
            nState = ACTIVE_VALIDATOR_INPUT_TOO_NEW;
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: %s - %d confirmations\n", GetStateString(), GetStatus(), GetInputAge(outpoint));
            return;
        }

        LOCK(pwalletMain->cs_wallet);
        pwalletMain->LockCoin(outpoint);
        pwalletMain->UnlockCoin(outpoint);

        bool fSignable = pwalletMain->SigningKey(pubKeyCollateral);
        if (!fSignable) {
            nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
            strNotCapableReason = "Validator signing key not available";
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: Found valid collateral!\n", GetStateString());
    } else {
        nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
        strNotCapableReason = "Could not find suitable coins!";
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // The service needs the correct default port to work properly
    if (!CValidatorBroadcast::CheckDefaultPort(service, strNotCapableReason, "CActiveValidator::ManageStateInitial"))
        return;

    nState = ACTIVE_VALIDATOR_STARTED;

    if (!fPingerEnabled) {
        fPingerEnabled = true;
    }

    LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateInitial -- %s: Validator successfully started\n", GetStateString());
}

void CActiveValidator::ManageStateRemote()
{
    LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyValidator.GetID() = %s\n",
        GetStatus(), GetTypeString(), fPingerEnabled, pubKeyValidator.GetID().ToString());

    mnodeman.CheckValidator(pubKeyValidator, true);
    validator_info_t infoMn;
    if (mnodeman.GetValidatorInfo(pubKeyValidator, infoMn)) {
        if (infoMn.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (service != infoMn.addr) {
            nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
            strNotCapableReason = "Bad service address";
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CValidator::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
            strNotCapableReason = strprintf("Validator in %s state", CValidator::StateToString(infoMn.nActiveState));
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (nState != ACTIVE_VALIDATOR_STARTED) {
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateRemote -- STARTED!\n");
            outpoint = infoMn.vin.prevout;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_VALIDATOR_STARTED;
        }
    } else {
        nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
        strNotCapableReason = "Validator not in validator list";
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveValidator::ManageStateLocal()
{
    LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
    if (nState == ACTIVE_VALIDATOR_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetValidatorOutpointAndKeys(outpoint, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(outpoint);
        if (nInputAge < nValidatorMinimumConfirmations) {
            nState = ACTIVE_VALIDATOR_INPUT_TOO_NEW;
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateLocal -- %s: %s - %d confirmations\n", GetStateString(), GetStatus(), nInputAge);
            return;
        }

        LOCK(pwalletMain->cs_wallet);
        pwalletMain->LockCoin(outpoint);
        pwalletMain->UnlockCoin(outpoint);

        CValidatorBroadcast mnb;
        std::string strError;
        if (!CValidatorBroadcast::Create(outpoint, service, keyCollateral, pubKeyCollateral, keyValidator, pubKeyValidator, strError, mnb)) {
            nState = ACTIVE_VALIDATOR_NOT_CAPABLE;
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        nState = ACTIVE_VALIDATOR_STARTED;

        // update to validator list
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateLocal -- Update Validator List\n");
        mnodeman.UpdateValidatorList(mnb);
        mnodeman.NotifyValidatorUpdates(*g_connman);

        // send to all peers
        LogPrint(BCLog::VALIDATOR, "CActiveValidator::ManageStateLocal -- Relay broadcast, collateral=%s\n", outpoint.ToStringShort());
        mnb.Relay();
    }
}