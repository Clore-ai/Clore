// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_MASTERNODE_SYNC_H
#define CLORE_MASTERNODE_SYNC_H

#include "chain.h"
#include "net.h"

#include <univalue.h>

class CMasternodeSync;

static const int MASTERNODE_SYNC_FAILED = -1;
static const int MASTERNODE_SYNC_INITIAL = 0;
static const int MASTERNODE_SYNC_SPORKS = 1;
static const int MASTERNODE_SYNC_LIST = 2;
static const int MASTERNODE_SYNC_MNW = 3;
static const int MASTERNODE_SYNC_FINISHED = 999;

static const int MASTERNODE_SYNC_TICK_SECONDS = 6;
static const int MASTERNODE_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine

static const int MASTERNODE_SYNC_ENOUGH_PEERS = 6;

extern CMasternodeSync masternodeSync;

//
// CMasternodeSync : Sync masternode assets in stages
//

class CMasternodeSync
{
private:
    // Keep track of current asset
    int nRequestedMasternodeAssets;
    // Count peers we've requested the asset from
    int nRequestedMasternodeAttempt;

    // Time when current masternode asset sync started
    int64_t nTimeAssetSyncStarted;

    // Last time when we received some masternode asset ...
    int64_t nTimeLastMasternodeList;
    int64_t nTimeLastPaymentVote;

    // ... or failed
    int64_t nTimeLastFailure;

    // How many times we failed
    int nCountFailures;

    // Keep track of current block index
    const CBlockIndex* pCurrentBlockIndex;

    bool CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes = false);
    void Fail();
    void SwitchToNextAsset();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

public:
    CMasternodeSync() { Reset(); }

    SERIALIZE_METHODS(CMasternodeSync, obj)
    {
        READWRITE(obj.nRequestedMasternodeAssets, obj.nRequestedMasternodeAttempt, obj.nTimeAssetSyncStarted,
            obj.nTimeLastMasternodeList, obj.nTimeLastPaymentVote, obj.nTimeLastFailure, obj.nCountFailures);
    }

    void SendGovernanceSyncRequest(CNode* pnode);

    bool IsFailed() { return nRequestedMasternodeAssets == MASTERNODE_SYNC_FAILED; }
    bool IsBlockchainSynced() { return nRequestedMasternodeAssets > MASTERNODE_SYNC_SPORKS; }
    bool IsMasternodeListSynced() { return nRequestedMasternodeAssets > MASTERNODE_SYNC_LIST; }
    bool IsWinnersListSynced() { return nRequestedMasternodeAssets > MASTERNODE_SYNC_MNW; }
    bool IsSynced() { return nRequestedMasternodeAssets == MASTERNODE_SYNC_FINISHED; }

    int GetAssetID() { return nRequestedMasternodeAssets; }
    int GetAttempt() { return nRequestedMasternodeAttempt; }
    std::string GetAssetName();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset();

    void ProcessTick();

    void AcceptedBlockHeader(const CBlockIndex* pindexNew);
    void NotifyHeaderTip(const CBlockIndex* pindexNew, bool fInitialDownload);
    void UpdatedBlockTip(const CBlockIndex* pindexNew, bool fInitialDownload);

    // CLORE: Check if masternode sync is enabled
    bool IsEnabled() const;
};

#endif // CLORE_MASTERNODE_SYNC_H