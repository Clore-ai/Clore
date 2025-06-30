// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-sync.h"

#include "activemasternode.h"
#include "chainparams.h"
#include "masternode-payments.h"
#include "masternodeman.h"
#include "netmessagemaker.h"
#include "ui_interface.h"
#include "util.h"

class CMasternodeSync;
CMasternodeSync masternodeSync;

bool CMasternodeSync::CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes)
{
    CNodeStateStats stats;
    if (!GetNodeStateStats(pnode->GetId(), stats) || stats.nCommonHeight == -1 || stats.nSyncHeight == -1) return false; // not enough info about this peer

    // Check blocks and headers, allow a small error margin of 1 block
    if (pCurrentBlockIndex->nHeight - 1 > stats.nCommonHeight) {
        // This peer probably stuck, don't sync any additional data from it
        if (fDisconnectStuckNodes) {
            // Disconnect to free this connection slot for another peer.
            pnode->fDisconnect = true;
            LogPrintf("CMasternodeSync::CheckNodeHeight -- disconnecting from stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->GetId());
        } else {
            LogPrintf("CMasternodeSync::CheckNodeHeight -- skipping stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->GetId());
        }
        return false;
    } else if (pCurrentBlockIndex->nHeight < stats.nSyncHeight - 1) {
        // This peer has more blocks than us, don't sync any additional data from it
        LogPrintf("CMasternodeSync::CheckNodeHeight -- skipping peer ahead of us, nHeight=%d, nSyncHeight=%d, peer=%d\n",
            pCurrentBlockIndex->nHeight, stats.nSyncHeight, pnode->GetId());
        return false;
    }

    return true;
}

bool CMasternodeSync::IsEnabled() const
{
    // CLORE: Masternode sync is enabled only if payments are enabled
    return fMasternodePaymentsEnabled;
}

bool CMasternodeSync::IsBlockchainSynced()
{
    // CLORE: If masternode sync is disabled, consider blockchain always synced
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
        LogPrintf("CMasternodeSync::IsBlockchainSynced time-check fBlockchainSynced=%s\n", fBlockchainSynced);
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

    LogPrintf("CMasternodeSync::IsBlockchainSynced -- blockchain is considered synced\n");
    fBlockchainSynced = true;

    return true;
}

void CMasternodeSync::Fail()
{
    nTimeLastFailure = GetTime();
    nRequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
}

void CMasternodeSync::Reset()
{
    nRequestedMasternodeAssets = MASTERNODE_SYNC_INITIAL;
    nRequestedMasternodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastMasternodeList = 0;
    nTimeLastPaymentVote = 0;
    nTimeLastFailure = 0;
    nCountFailures = 0;
}

std::string CMasternodeSync::GetAssetName()
{
    switch (nRequestedMasternodeAssets) {
    case (MASTERNODE_SYNC_INITIAL):
        return "MASTERNODE_SYNC_INITIAL";
    case (MASTERNODE_SYNC_SPORKS):
        return "MASTERNODE_SYNC_SPORKS";
    case (MASTERNODE_SYNC_LIST):
        return "MASTERNODE_SYNC_LIST";
    case (MASTERNODE_SYNC_MNW):
        return "MASTERNODE_SYNC_MNW";
    case (MASTERNODE_SYNC_FAILED):
        return "MASTERNODE_SYNC_FAILED";
    case MASTERNODE_SYNC_FINISHED:
        return "MASTERNODE_SYNC_FINISHED";
    default:
        return "UNKNOWN";
    }
}

void CMasternodeSync::SwitchToNextAsset()
{
    switch (nRequestedMasternodeAssets) {
    case (MASTERNODE_SYNC_FAILED):
        throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
        break;
    case (MASTERNODE_SYNC_INITIAL):
        nRequestedMasternodeAssets = MASTERNODE_SYNC_SPORKS;
        LogPrintf("CMasternodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
        break;
    case (MASTERNODE_SYNC_SPORKS):
        nRequestedMasternodeAssets = MASTERNODE_SYNC_LIST;
        LogPrintf("CMasternodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
        break;
    case (MASTERNODE_SYNC_LIST):
        nRequestedMasternodeAssets = MASTERNODE_SYNC_MNW;
        LogPrintf("CMasternodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
        break;
    case (MASTERNODE_SYNC_MNW):
        LogPrintf("CMasternodeSync::SwitchToNextAsset -- Sync has finished\n");
        nRequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
        uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
        break;
    }
    nRequestedMasternodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
}

std::string CMasternodeSync::GetSyncStatus()
{
    switch (nRequestedMasternodeAssets) {
    case MASTERNODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case MASTERNODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case MASTERNODE_SYNC_LIST:
        return _("Synchronizing masternodes...");
    case MASTERNODE_SYNC_MNW:
        return _("Synchronizing masternode payments...");
    case MASTERNODE_SYNC_FAILED:
        return _("Synchronization failed");
    case MASTERNODE_SYNC_FINISHED:
        return _("Synchronization finished");
    default:
        return "";
    }
}

void CMasternodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // CLORE: If masternode sync is disabled, ignore all sync messages
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

void CMasternodeSync::ProcessTick()
{
    static int nTick = 0;
    if (nTick++ % MASTERNODE_SYNC_TICK_SECONDS != 0) return;
    if (!pCurrentBlockIndex) return;

    // CLORE: If masternode sync is disabled, mark as finished
    if (!IsEnabled()) {
        if (nRequestedMasternodeAssets != MASTERNODE_SYNC_FINISHED) {
            nRequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
            LogPrintf("CMasternodeSync::ProcessTick -- Masternode sync disabled, marking as finished\n");
        }
        return;
    }

    // The initial timeout varies by asset and is either 8 minutes or 1/4 of the masternode count (whichever is greater)
    static const int64_t nTimeoutBase = 8 * 60; // 8 minutes
    int64_t nTimeoutMultiplier = std::max(1, mnodeman.size() / 4);
    int64_t nTimeout = nTimeoutBase * nTimeoutMultiplier;

    // SPORKS : always sync
    if (nRequestedMasternodeAssets == MASTERNODE_SYNC_SPORKS) {
        // No sporks in CLORE for now, just move to next asset
        SwitchToNextAsset();
        return;
    }

    // INITIAL : sync blockchain
    if (nRequestedMasternodeAssets == MASTERNODE_SYNC_INITIAL) {
        if (IsBlockchainSynced()) {
            SwitchToNextAsset();
            return;
        }
    }

    // MNLIST : sync masternode list from other connected clients
    if (nRequestedMasternodeAssets == MASTERNODE_SYNC_LIST) {
        LogPrint(BCLog::MASTERNODE, "CMasternodeSync::ProcessTick -- nTick %d nRequestedMasternodeAssets %d nTimeLastMasternodeList %lld GetTime() %lld diff %lld\n", nTick, nRequestedMasternodeAssets, nTimeLastMasternodeList, GetTime(), GetTime() - nTimeLastMasternodeList);

        // Check for timeout first
        if (GetTime() - nTimeAssetSyncStarted > nTimeout) {
            LogPrintf("CMasternodeSync::ProcessTick -- nTick %d nRequestedMasternodeAssets %d -- timeout\n", nTick, nRequestedMasternodeAssets);
            if (nRequestedMasternodeAttempt == 0) {
                LogPrintf("CMasternodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                // There is no way we can continue without masternode list, fail here and try later
                Fail();
                return;
            }
            SwitchToNextAsset();
            return;
        }

        // Only request once from each peer
        if (nRequestedMasternodeAttempt > 0) return;
        nRequestedMasternodeAttempt++;

        g_connman->ForEachNode([](CNode* pnode) {
            if (pnode->nVersion < MIN_MASTERNODE_PAYMENT_PROTO_VERSION) return;
            if (!masternodeSync.CheckNodeHeight(pnode)) return;

            g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make("dseg", CTxIn()));
        });

        return; // always exit after the first call
    }

    // MNW : sync masternode payment votes from other connected clients
    if (nRequestedMasternodeAssets == MASTERNODE_SYNC_MNW) {
        LogPrint(BCLog::MASTERNODE, "CMasternodeSync::ProcessTick -- nTick %d nRequestedMasternodeAssets %d nTimeLastPaymentVote %lld GetTime() %lld diff %lld\n", nTick, nRequestedMasternodeAssets, nTimeLastPaymentVote, GetTime(), GetTime() - nTimeLastPaymentVote);

        // Check for timeout first
        if (GetTime() - nTimeAssetSyncStarted > nTimeout) {
            LogPrintf("CMasternodeSync::ProcessTick -- nTick %d nRequestedMasternodeAssets %d -- timeout\n", nTick, nRequestedMasternodeAssets);
            if (nRequestedMasternodeAttempt == 0) {
                LogPrintf("CMasternodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                // Probably not a good idea to proceed without winner list
                Fail();
                return;
            }
            SwitchToNextAsset();
            return;
        }

        // Check for data
        if (masternodePayments.GetBlockCount() > 0) {
            LogPrintf("CMasternodeSync::ProcessTick -- nTick %d nRequestedMasternodeAssets %d -- found enough data\n", nTick, nRequestedMasternodeAssets);
            SwitchToNextAsset();
            return;
        }

        // Only request once from each peer
        if (nRequestedMasternodeAttempt > 0) return;
        nRequestedMasternodeAttempt++;

        g_connman->ForEachNode([](CNode* pnode) {
            if (pnode->nVersion < MIN_MASTERNODE_PAYMENT_PROTO_VERSION) return;
            if (!masternodeSync.CheckNodeHeight(pnode)) return;

            int nMnCount = mnodeman.CountEnabled();
            masternodePayments.Sync(pnode, nMnCount);
        });

        return; // always exit after the first call
    }
}

void CMasternodeSync::AcceptedBlockHeader(const CBlockIndex* pindexNew)
{
    LogPrint(BCLog::MASTERNODE, "CMasternodeSync::AcceptedBlockHeader -- pindexNew->nHeight: %d\n", pindexNew->nHeight);

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block header arrives while we're still syncing blockchain
        BumpAssetLastTime("CMasternodeSync::AcceptedBlockHeader");
    }
}

void CMasternodeSync::NotifyHeaderTip(const CBlockIndex* pindexNew, bool fInitialDownload)
{
    LogPrint(BCLog::MASTERNODE, "CMasternodeSync::NotifyHeaderTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we're still syncing blockchain
        BumpAssetLastTime("CMasternodeSync::NotifyHeaderTip");
    }
}

void CMasternodeSync::UpdatedBlockTip(const CBlockIndex* pindexNew, bool fInitialDownload)
{
    LogPrint(BCLog::MASTERNODE, "CMasternodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    pCurrentBlockIndex = pindexNew;
}