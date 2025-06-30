// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "addrman.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "netbase.h"
#include "protocol.h"
#include "spork.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "util.h"

// Keep track of the active Masternode
CActiveMasternode activeMasternode;

void CActiveMasternode::ManageState()
{
    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageState -- Start\n");
    if (!fMasternodeMode) {
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageState -- Not a masternode, returning\n");
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !masternodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if (nState == ACTIVE_MASTERNODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_MASTERNODE_INITIAL;
    }

    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if (eType == MASTERNODE_UNKNOWN) {
        ManageStateInitial();
    }

    if (eType == MASTERNODE_REMOTE) {
        ManageStateRemote();
    } else if (eType == MASTERNODE_LOCAL) {
        // Try Remote Start first so the started local masternode can be restarted without recreate masternode broadcast.
        ManageStateRemote();
        if (nState != ACTIVE_MASTERNODE_STARTED)
            ManageStateLocal();
    }

    SendMasternodePing();
}

std::string CActiveMasternode::GetStateString() const
{
    switch (nState) {
    case ACTIVE_MASTERNODE_INITIAL:
        return "INITIAL";
    case ACTIVE_MASTERNODE_SYNC_IN_PROCESS:
        return "SYNC_IN_PROCESS";
    case ACTIVE_MASTERNODE_INPUT_TOO_NEW:
        return "INPUT_TOO_NEW";
    case ACTIVE_MASTERNODE_NOT_CAPABLE:
        return "NOT_CAPABLE";
    case ACTIVE_MASTERNODE_STARTED:
        return "STARTED";
    default:
        return "UNKNOWN";
    }
}

std::string CActiveMasternode::GetStatus() const
{
    switch (nState) {
    case ACTIVE_MASTERNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_MASTERNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Masternode";
    case ACTIVE_MASTERNODE_INPUT_TOO_NEW:
        return strprintf("Masternode input must have at least %d confirmations", nMasternodeMinimumConfirmations);
    case ACTIVE_MASTERNODE_NOT_CAPABLE:
        return "Not capable masternode: " + strNotCapableReason;
    case ACTIVE_MASTERNODE_STARTED:
        return "Masternode successfully started";
    default:
        return "Unknown";
    }
}

std::string CActiveMasternode::GetTypeString() const
{
    std::string strType;
    switch (eType) {
    case MASTERNODE_REMOTE:
        strType = "REMOTE";
        break;
    case MASTERNODE_LOCAL:
        strType = "LOCAL";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveMasternode::SendMasternodePing()
{
    if (!fPingerEnabled) {
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::SendMasternodePing -- %s: masternode ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if (!mnodeman.Has(outpoint)) {
        strNotCapableReason = "Masternode not in masternode list";
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::SendMasternodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CMasternodePing mnp(outpoint);
    if (!mnp.Sign(keyMasternode, pubKeyMasternode)) {
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::SendMasternodePing -- ERROR: Couldn't sign Masternode Ping\n");
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    if (mnodeman.IsMasternodePingedWithin(outpoint, MASTERNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::SendMasternodePing -- Too early to send Masternode Ping\n");
        return false;
    }

    mnodeman.SetLastPing(outpoint, mnp);

    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::SendMasternodePing -- Relaying ping, collateral=%s\n", outpoint.ToStringShort());
    mnp.Relay();

    return true;
}

bool CActiveMasternode::UpdateSentinelPing(int version)
{
    nSentinelVersion = version;
    return SendMasternodePing();
}

void CActiveMasternode::ManageStateInitial()
{
    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Masternode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes) {
            if (pnode->addr == service) {
                fFoundLocal = true;
                LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- found local connection\n");
                break;
            }
        }
    }

    if (!fFoundLocal) {
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != 9999) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid port: " + std::to_string(service.GetPort()) + " - only 9999 is supported on mainnet.";
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if (service.GetPort() == 9999) {
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Invalid port: " + std::to_string(service.GetPort()) + " - 9999 is only supported on mainnet.";
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());

    if (!ConnectNode((CAddress)service, NULL, true)) {
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = MASTERNODE_REMOTE;

    // Check if wallet funds are available
    if (!pwalletMain) {
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if (pwalletMain->IsLocked()) {
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if (pwalletMain->GetBalance() < 1000 * COIN) {
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: Wallet balance is < 1000 CLORE\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetMasternodeOutpointAndKeys(outpoint, pubKeyCollateral, keyCollateral)) {
        if (GetInputAge(outpoint) < nMasternodeMinimumConfirmations) {
            nState = ACTIVE_MASTERNODE_INPUT_TOO_NEW;
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: %s - %d confirmations\n", GetStateString(), GetStatus(), GetInputAge(outpoint));
            return;
        }

        LOCK(pwalletMain->cs_wallet);
        pwalletMain->LockCoin(outpoint);
        pwalletMain->UnlockCoin(outpoint);

        bool fSignable = pwalletMain->SigningKey(pubKeyCollateral);
        if (!fSignable) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Masternode signing key not available";
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: Found valid collateral!\n", GetStateString());
    } else {
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Could not find suitable coins!";
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // The service needs the correct default port to work properly
    if (!CMasternodeBroadcast::CheckDefaultPort(service, strNotCapableReason, "CActiveMasternode::ManageStateInitial"))
        return;

    nState = ACTIVE_MASTERNODE_STARTED;

    if (!fPingerEnabled) {
        fPingerEnabled = true;
    }

    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateInitial -- %s: Masternode successfully started\n", GetStateString());
}

void CActiveMasternode::ManageStateRemote()
{
    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyMasternode.GetID() = %s\n",
        GetStatus(), GetTypeString(), fPingerEnabled, pubKeyMasternode.GetID().ToString());

    mnodeman.CheckMasternode(pubKeyMasternode, true);
    masternode_info_t infoMn;
    if (mnodeman.GetMasternodeInfo(pubKeyMasternode, infoMn)) {
        if (infoMn.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (service != infoMn.addr) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Bad service address";
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CMasternode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Masternode in %s state", CMasternode::StateToString(infoMn.nActiveState));
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (nState != ACTIVE_MASTERNODE_STARTED) {
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateRemote -- STARTED!\n");
            outpoint = infoMn.vin.prevout;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_MASTERNODE_STARTED;
        }
    } else {
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Masternode not in masternode list";
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveMasternode::ManageStateLocal()
{
    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
    if (nState == ACTIVE_MASTERNODE_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetMasternodeOutpointAndKeys(outpoint, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(outpoint);
        if (nInputAge < nMasternodeMinimumConfirmations) {
            nState = ACTIVE_MASTERNODE_INPUT_TOO_NEW;
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateLocal -- %s: %s - %d confirmations\n", GetStateString(), GetStatus(), nInputAge);
            return;
        }

        LOCK(pwalletMain->cs_wallet);
        pwalletMain->LockCoin(outpoint);
        pwalletMain->UnlockCoin(outpoint);

        CMasternodeBroadcast mnb;
        std::string strError;
        if (!CMasternodeBroadcast::Create(outpoint, service, keyCollateral, pubKeyCollateral, keyMasternode, pubKeyMasternode, strError, mnb)) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        nState = ACTIVE_MASTERNODE_STARTED;

        // update to masternode list
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateLocal -- Update Masternode List\n");
        mnodeman.UpdateMasternodeList(mnb);
        mnodeman.NotifyMasternodeUpdates(*g_connman);

        // send to all peers
        LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStateLocal -- Relay broadcast, collateral=%s\n", outpoint.ToStringShort());
        mnb.Relay();
    }
}