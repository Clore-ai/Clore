// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validator-sync.h"

#include "activevalidator.h"
#include "chainparams.h"
#include "netmessagemaker.h"
#include "ui_interface.h"
#include "util.h"
#include "validator-payments.h"
#include "validatorman.h"

class CValidatorSync;
CValidatorSync validatorSync;

bool CValidatorSync::CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes)
{
    CNodeStateStats stats;
    if (!GetNodeStateStats(pnode->GetId(), stats) || stats.nCommonHeight == -1 || stats.nSyncHeight == -1) return false; // not enough info about this peer

    // Check blocks and headers, allow a small error margin of 1 block
    if (pCurrentBlockIndex->nHeight - 1 > stats.nCommonHeight) {
        // This peer probably stuck, don't sync any additional data from it
        if (fDisconnectStuckNodes) {
            // Disconnect to free this connection slot for another peer.
            pnode->fDisconnect = true;
            LogPrintf("CValidatorSync::CheckNodeHeight -- disconnecting from stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->GetId());
        } else {
            LogPrintf("CValidatorSync::CheckNodeHeight -- skipping stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->GetId());
        }
        return false;
    } else if (pCurrentBlockIndex->nHeight < stats.nSyncHeight - 1) {
        // This peer has more blocks than us, don't sync any additional data from it
        LogPrintf("CValidatorSync::CheckNodeHeight -- skipping peer ahead of us, nHeight=%d, nSyncHeight=%d, peer=%d\n",
            pCurrentBlockIndex->nHeight, stats.nSyncHeight, pnode->GetId());
        return false;
    }

    return true;
}

bool CValidatorSync::IsEnabled() const
{
    // CLORE: Validator sync is enabled only if payments are enabled
    return fValidatorPaymentsEnabled;
}

bool CValidatorSync::IsBlockchainSynced()
{
    // CLORE: If validator sync is disabled, consider blockchain always synced
    if (!IsEnabled()) {
        return true;
    }

    static bool fBlockchainSynced = false;
    static int64_t nTimeLastProcess = GetTime();
    static int nSkipped = 0;
    static bool fFirstBlockAccepted = false;

    // If the last call to this function was more than 60 minutes ago (client was in sleep mode)
    // reset the sync process
    if (GetTime() - nTimeLastProcess > 60 * 60) {
        LogPrintf("CValidatorSync::IsBlockchainSynced time-check fBlockchainSynced=%s\n", fBlockchainSynced);
        Reset();
        fBlockchainSynced = false;
    }
    nTimeLastProcess = GetTime();

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;

    if (!pCurrentBlockIndex) {
        pCurrentBlockIndex = pindex;
        return false;
    }

    if (pCurrentBlockIndex->GetBlockHash() == pindex->GetBlockHash()) {
        nSkipped++;
        if (nSkipped < 10) return false;
        nSkipped = 0;
    } else {
        nSkipped = 0;
        pCurrentBlockIndex = pindex;
    }

    LogPrintf("CValidatorSync::IsBlockchainSynced -- blockchain is considered synced\n");
    fBlockchainSynced = true;

    return true;
}

void CValidatorSync::Fail()
{
    nTimeLastFailure = GetTime();
    nRequestedValidatorAssets = VALIDATOR_SYNC_FAILED;
}

void CValidatorSync::Reset()
{
    nRequestedValidatorAssets = VALIDATOR_SYNC_INITIAL;
    nRequestedValidatorAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastValidatorList = 0;
    nTimeLastPaymentVote = 0;
    nTimeLastFailure = 0;
    nCountFailures = 0;
}

std::string CValidatorSync::GetAssetName()
{
    switch (nRequestedValidatorAssets) {
    case (VALIDATOR_SYNC_INITIAL):
        return "VALIDATOR_SYNC_INITIAL";
    case (VALIDATOR_SYNC_SPORKS):
        return "VALIDATOR_SYNC_SPORKS";
    case (VALIDATOR_SYNC_LIST):
        return "VALIDATOR_SYNC_LIST";
    case (VALIDATOR_SYNC_MNW):
        return "VALIDATOR_SYNC_MNW";
    case (VALIDATOR_SYNC_FAILED):
        return "VALIDATOR_SYNC_FAILED";
    case VALIDATOR_SYNC_FINISHED:
        return "VALIDATOR_SYNC_FINISHED";
    default:
        return "UNKNOWN";
    }
}

void CValidatorSync::SwitchToNextAsset()
{
    switch (nRequestedValidatorAssets) {
    case (VALIDATOR_SYNC_FAILED):
        throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
        break;
    case (VALIDATOR_SYNC_INITIAL):
        nRequestedValidatorAssets = VALIDATOR_SYNC_SPORKS;
        LogPrintf("CValidatorSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
        break;
    case (VALIDATOR_SYNC_SPORKS):
        nRequestedValidatorAssets = VALIDATOR_SYNC_LIST;
        LogPrintf("CValidatorSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
        break;
    case (VALIDATOR_SYNC_LIST):
        nRequestedValidatorAssets = VALIDATOR_SYNC_MNW;
        LogPrintf("CValidatorSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
        break;
    case (VALIDATOR_SYNC_MNW):
        LogPrintf("CValidatorSync::SwitchToNextAsset -- Sync has finished\n");
        nRequestedValidatorAssets = VALIDATOR_SYNC_FINISHED;
        uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
        break;
    }
    nRequestedValidatorAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
}

std::string CValidatorSync::GetSyncStatus()
{
    switch (nRequestedValidatorAssets) {
    case VALIDATOR_SYNC_INITIAL:
        return _("Synchronization pending...");
    case VALIDATOR_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case VALIDATOR_SYNC_LIST:
        return _("Synchronizing validators...");
    case VALIDATOR_SYNC_MNW:
        return _("Synchronizing validator payments...");
    case VALIDATOR_SYNC_FAILED:
        return _("Synchronization failed");
    case VALIDATOR_SYNC_FINISHED:
        return _("Synchronization finished");
    default:
        return "";
    }
}

void CValidatorSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // CLORE: If validator sync is disabled, ignore all sync messages
    if (!IsEnabled()) {
        return;
    }

    if (strCommand == "ssc") { // Sync status count
        if (IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->GetId());
    }
}

void CValidatorSync::ProcessTick()
{
    static int nTick = 0;
    if (nTick++ % VALIDATOR_SYNC_TICK_SECONDS != 0) return;
    if (!pCurrentBlockIndex) return;

    // CLORE: If validator sync is disabled, mark as finished
    if (!IsEnabled()) {
        if (nRequestedValidatorAssets != VALIDATOR_SYNC_FINISHED) {
            nRequestedValidatorAssets = VALIDATOR_SYNC_FINISHED;
            LogPrintf("CValidatorSync::ProcessTick -- Validator sync disabled, marking as finished\n");
        }
        return;
    }

    // The initial timeout varies by asset and is either 8 minutes or 1/4 of the validator count (whichever is greater)
    static const int64_t nTimeoutBase = 8 * 60; // 8 minutes
    int64_t nTimeoutMultiplier = std::max(1, mnodeman.size() / 4);
    int64_t nTimeout = nTimeoutBase * nTimeoutMultiplier;

    // SPORKS : always sync
    if (nRequestedValidatorAssets == VALIDATOR_SYNC_SPORKS) {
        // No sporks in CLORE for now, just move to next asset
        SwitchToNextAsset();
        return;
    }

    // INITIAL : sync blockchain
    if (nRequestedValidatorAssets == VALIDATOR_SYNC_INITIAL) {
        if (IsBlockchainSynced()) {
            SwitchToNextAsset();
            return;
        }
    }

    // MNLIST : sync validator list from other connected clients
    if (nRequestedValidatorAssets == VALIDATOR_SYNC_LIST) {
        LogPrint(BCLog::VALIDATOR, "CValidatorSync::ProcessTick -- nTick %d nRequestedValidatorAssets %d nTimeLastValidatorList %lld GetTime() %lld diff %lld\n", nTick, nRequestedValidatorAssets, nTimeLastValidatorList, GetTime(), GetTime() - nTimeLastValidatorList);

        // Check for timeout first
        if (GetTime() - nTimeAssetSyncStarted > nTimeout) {
            LogPrintf("CValidatorSync::ProcessTick -- nTick %d nRequestedValidatorAssets %d -- timeout\n", nTick, nRequestedValidatorAssets);
            if (nRequestedValidatorAttempt == 0) {
                LogPrintf("CValidatorSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                // There is no way we can continue without validator list, fail here and try later
                Fail();
                return;
            }
            SwitchToNextAsset();
            return;
        }

        // Only request once from each peer
        if (nRequestedValidatorAttempt > 0) return;
        nRequestedValidatorAttempt++;

        g_connman->ForEachNode([](CNode* pnode) {
            if (pnode->nVersion < MIN_VALIDATOR_PAYMENT_PROTO_VERSION) return;
            if (!validatorSync.CheckNodeHeight(pnode)) return;

            g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make("dseg", CTxIn()));
        });

        return; // always exit after the first call
    }

    // MNW : sync validator payment votes from other connected clients
    if (nRequestedValidatorAssets == VALIDATOR_SYNC_MNW) {
        LogPrint(BCLog::VALIDATOR, "CValidatorSync::ProcessTick -- nTick %d nRequestedValidatorAssets %d nTimeLastPaymentVote %lld GetTime() %lld diff %lld\n", nTick, nRequestedValidatorAssets, nTimeLastPaymentVote, GetTime(), GetTime() - nTimeLastPaymentVote);

        // Check for timeout first
        if (GetTime() - nTimeAssetSyncStarted > nTimeout) {
            LogPrintf("CValidatorSync::ProcessTick -- nTick %d nRequestedValidatorAssets %d -- timeout\n", nTick, nRequestedValidatorAssets);
            if (nRequestedValidatorAttempt == 0) {
                LogPrintf("CValidatorSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                // Probably not a good idea to proceed without winner list
                Fail();
                return;
            }
            SwitchToNextAsset();
            return;
        }

        // Check for data
        if (validatorPayments.GetBlockCount() > 0) {
            LogPrintf("CValidatorSync::ProcessTick -- nTick %d nRequestedValidatorAssets %d -- found enough data\n", nTick, nRequestedValidatorAssets);
            SwitchToNextAsset();
            return;
        }

        // Only request once from each peer
        if (nRequestedValidatorAttempt > 0) return;
        nRequestedValidatorAttempt++;

        g_connman->ForEachNode([](CNode* pnode) {
            if (pnode->nVersion < MIN_VALIDATOR_PAYMENT_PROTO_VERSION) return;
            if (!validatorSync.CheckNodeHeight(pnode)) return;

            int nMnCount = mnodeman.CountEnabled();
            validatorPayments.Sync(pnode, nMnCount);
        });

        return; // always exit after the first call
    }
}

void CValidatorSync::AcceptedBlockHeader(const CBlockIndex* pindexNew)
{
    LogPrint(BCLog::VALIDATOR, "CValidatorSync::AcceptedBlockHeader -- pindexNew->nHeight: %d\n", pindexNew->nHeight);

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block header arrives while we're still syncing blockchain
        BumpAssetLastTime("CValidatorSync::AcceptedBlockHeader");
    }
}

void CValidatorSync::NotifyHeaderTip(const CBlockIndex* pindexNew, bool fInitialDownload)
{
    LogPrint(BCLog::VALIDATOR, "CValidatorSync::NotifyHeaderTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we're still syncing blockchain
        BumpAssetLastTime("CValidatorSync::NotifyHeaderTip");
    }
}

void CValidatorSync::UpdatedBlockTip(const CBlockIndex* pindexNew, bool fInitialDownload)
{
    LogPrint(BCLog::VALIDATOR, "CValidatorSync::UpdatedBlockTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    pCurrentBlockIndex = pindexNew;
}