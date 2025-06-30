// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodeman.h"
#include "activemasternode.h"
#include "addrman.h"
#include "masternode.h"
#include "obfuscation.h"
#include "spork.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "util.h"
#include <boost/filesystem.hpp>

#define MASTERNODES_DUMP_SECONDS (15 * 60)
#define MASTERNODES_DSEG_SECONDS (3 * 60 * 60)

CMasternodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const std::pair<int64_t, CTxIn>& t1,
        const std::pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const std::pair<int64_t, CTxIn>& t1,
        const std::pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const std::pair<int64_t, CMasternode>& t1,
        const std::pair<int64_t, CMasternode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CMasternodeDB
//

CMasternodeDB::CMasternodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "MasternodeCache";
}

bool CMasternodeDB::Write(const CMasternodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssMasternodes(SER_DISK, CLIENT_VERSION);
    ssMasternodes << strMagicMessage;          // masternode cache file specific magic message
    ssMasternodes << FLATFILE(CLIENT_VERSION); // masternode cache file format version
    mnodemanToSave.Serialize(ssMasternodes);

    uint256 hash = Hash(ssMasternodes.begin(), ssMasternodes.end());
    ssMasternodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssMasternodes;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    FileCommit(fileout.Get());
    fileout.fclose();

    LogPrint(BCLog::MASTERNODE, "Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::MASTERNODE, "  %s\n", mnodemanToSave.ToString());

    return true;
}

CMasternodeDB::ReadResult CMasternodeDB::Read(CMasternodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    std::vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (const std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssMasternodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssMasternodes.begin(), ssMasternodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssMasternodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssMasternodes >> FLATFILE(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CMasternodeMan object
        ssMasternodes >> mnodemanToLoad;
    } catch (const std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::MASTERNODE, "Loaded info from mncache.dat (dbversion=%d) %dms\n", mnodemanToLoad.nDbVersion, GetTimeMillis() - nStart);
    LogPrint(BCLog::MASTERNODE, "  %s\n", mnodemanToLoad.ToString());

    return Ok;
}

void DumpMasternodes()
{
    int64_t nStart = GetTimeMillis();

    CMasternodeDB mndb;
    CMasternodeMan tempMnodeman;

    LogPrint(BCLog::MASTERNODE, "Verifying mncache.dat format...\n");
    CMasternodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMasternodeDB::FileError)
        LogPrint(BCLog::MASTERNODE, "Missing masternode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CMasternodeDB::Ok) {
        LogPrint(BCLog::MASTERNODE, "Error reading mncache.dat: ");
        if (readResult == CMasternodeDB::IncorrectFormat)
            LogPrint(BCLog::MASTERNODE, "magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint(BCLog::MASTERNODE, "file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint(BCLog::MASTERNODE, "Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint(BCLog::MASTERNODE, "Masternode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CMasternodeMan::CMasternodeMan() : cvLastBlockHashes(CACHED_BLOCK_HASHES, UINT256_ZERO)
{
    SetLastPaid();
}

bool CMasternodeMan::Add(CMasternode& mn)
{
    LOCK(cs);

    if (Has(mn.vin.prevout)) return false;

    LogPrint(BCLog::MASTERNODE, "CMasternodeMan::Add -- Adding new Masternode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
    vMasternodes.push_back(mn);
    return true;
}

void CMasternodeMan::AskForMN(CNode* pnode, const CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);
    if (i != mWeAskedForMasternodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint(BCLog::MASTERNODE, "CMasternodeMan::AskForMN -- Asking node for missing entry, vin: %s\n", vin.prevout.ToStringShort());
    g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::GETMNLIST, vin));
    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mWeAskedForMasternodeListEntry[vin.prevout] = askAgain;
}

void CMasternodeMan::Check()
{
    LOCK(cs);

    for (CMasternode& mn : vMasternodes) {
        mn.Check();
    }
}

void CMasternodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    // remove inactive and outdated
    auto it = vMasternodes.begin();
    while (it != vMasternodes.end()) {
        if ((*it).activeState == CMasternode::MASTERNODE_REMOVE ||
            (*it).activeState == CMasternode::MASTERNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CMasternode::MASTERNODE_EXPIRED) ||
            (*it).protocolVersion < MIN_MASTERNODE_PAYMENT_PROTO_VERSION) {
            LogPrint(BCLog::MASTERNODE, "CMasternodeMan::CheckAndRemove -- Removing inactive Masternode: %s  %i now\n", (*it).vin.prevout.ToStringShort(), size() - 1);

            // erase all of the broadcasts we've seen from this vin
            //  -- if we missed a few pings and the node was removed, this will allow us to get it back without them
            //     sending a brand new mnb
            std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
            while (it3 != mapSeenMasternodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    masternodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenMasternodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this masternode again if we see another ping
            std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
            while (it2 != mWeAskedForMasternodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForMasternodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vMasternodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Masternode list
    std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForMasternodeList.begin();
    while (it1 != mAskedUsForMasternodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForMasternodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Masternode list
    it1 = mWeAskedForMasternodeList.begin();
    while (it1 != mWeAskedForMasternodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForMasternodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Masternodes we've asked for
    std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
    while (it2 != mWeAskedForMasternodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForMasternodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenMasternodeBroadcast
    std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
    while (it3 != mapSeenMasternodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodeBroadcast.erase(it3++);
            masternodeSync.mapSeenSyncMNB.erase((*it3).first);
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenMasternodePing
    std::map<uint256, CMasternodePing>::iterator it4 = mapSeenMasternodePing.begin();
    while (it4 != mapSeenMasternodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CMasternodeMan::Clear()
{
    LOCK(cs);
    vMasternodes.clear();
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
    mWeAskedForMasternodeListEntry.clear();
    mapSeenMasternodeBroadcast.clear();
    mapSeenMasternodePing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
    indexMasternodes.Clear();
    indexMasternodesOld.Clear();
}

int CMasternodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? MIN_MASTERNODE_PAYMENT_PROTO_VERSION : protocolVersion;

    for (const CMasternode& mn : vMasternodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CMasternodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? MIN_MASTERNODE_PAYMENT_PROTO_VERSION : protocolVersion;

    for (const CMasternode& mn : vMasternodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node;
        LookupHost(strHost.c_str(), node, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
        case 1:
            ipv4++;
            break;
        case 2:
            ipv6++;
            break;
        case 3:
            onion++;
            break;
        }
    }
}

void CMasternodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMasternodeList.find(pnode->addr);
            if (it != mWeAskedForMasternodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint(BCLog::MASTERNODE, "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::GETMNLIST, CTxIn()));
    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mWeAskedForMasternodeList[pnode->addr] = askAgain;
}

CMasternode* CMasternodeMan::Find(const COutPoint& outpoint)
{
    LOCK(cs);
    return indexMasternodes.Get(outpoint);
}

CMasternode* CMasternodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);
    return indexMasternodes.Get(vin.prevout);
}

CMasternode* CMasternodeMan::Find(const CPubKey& pubKeyMasternode)
{
    LOCK(cs);
    for (auto& mn : vMasternodes) {
        if (mn.pubKeyMasternode == pubKeyMasternode)
            return &mn;
    }
    return NULL;
}

bool CMasternodeMan::Get(const CPubKey& pubKeyMasternode, CMasternode& masternode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    // However, locking cs_main and cs together is not safe, so we use a temporary mutex.
    // For details see commit 03b9f24fcc1f5721a51f79ee1f48b7d8e7c2adfb
    LOCK(cs);
    CMasternode* pmn = Find(pubKeyMasternode);
    if (!pmn)
        return false;
    masternode = *pmn;
    return true;
}

bool CMasternodeMan::Get(const CTxIn& vin, CMasternode& masternode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    // However, locking cs_main and cs together is not safe, so we use a temporary mutex.
    // For details see commit 03b9f24fcc1f5721a51f79ee1f48b7d8e7c2adfb
    LOCK(cs);
    CMasternode* pmn = Find(vin);
    if (!pmn)
        return false;
    masternode = *pmn;
    return true;
}

masternode_info_t CMasternodeMan::GetMasternodeInfo(const CTxIn& vin)
{
    masternode_info_t info;
    LOCK(cs);
    CMasternode* pmn = Find(vin);
    if (!pmn)
        return info;
    info = pmn->GetInfo();
    return info;
}

masternode_info_t CMasternodeMan::GetMasternodeInfo(const CPubKey& pubKeyMasternode)
{
    masternode_info_t info;
    LOCK(cs);
    CMasternode* pmn = Find(pubKeyMasternode);
    if (!pmn)
        return info;
    info = pmn->GetInfo();
    return info;
}

bool CMasternodeMan::Has(const CTxIn& vin)
{
    LOCK(cs);
    CMasternode* pmn = Find(vin);
    return (pmn != NULL);
}

bool CMasternodeMan::Has(const COutPoint& outpoint)
{
    LOCK(cs);
    return indexMasternodes.Contains(outpoint);
}

masternode_info_t CMasternode::GetInfo()
{
    masternode_info_t info;
    info.vin = vin;
    info.addr = addr;
    info.pubKeyCollateralAddress = pubKeyCollateralAddress;
    info.pubKeyMasternode = pubKeyMasternode;
    info.sigTime = sigTime;
    info.nLastDsq = nLastDsq;
    info.nTimeLastChecked = nTimeLastChecked;
    info.nTimeLastPaid = nTimeLastPaid;
    info.nTimeLastWatchdogVote = nTimeLastWatchdogVote;
    info.nActiveState = activeState;
    info.nProtocolVersion = protocolVersion;
    info.fInfoValid = true;
    return info;
}

bool CMasternodeMan::IsMasternodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CMasternode* pmn = Find(vin);
    if (!pmn)
        return false;
    return pmn->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CMasternodeMan::SetLastPaid()
{
    LOCK(cs);

    if (vMasternodes.empty()) {
        return;
    }

    int64_t nLastPaid = 0;
    CMasternode* pBestMasternode = NULL;

    for (CMasternode& mn : vMasternodes) {
        if (!mn.IsEnabled()) continue;

        // check protocol version
        if (mn.protocolVersion < MIN_MASTERNODE_PAYMENT_PROTO_VERSION) continue;

        // it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (masternodePayments.IsScheduled(mn, 0)) continue;

        // it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMasternodeMinimumConfirmations * 2.6 * 60) > GetAdjustedTime()) continue;

        // make sure it has as many confirmations as there are blocks
        if (mn.GetMasternodeInputAge() < nMasternodeMinimumConfirmations) continue;

        if (mn.nLastPaid > nLastPaid) {
            nLastPaid = mn.nLastPaid;
            pBestMasternode = &mn;
        }
    }

    if (pBestMasternode) {
        LogPrint(BCLog::MASTERNODE, "CMasternodeMan::SetLastPaid -- found: %s with %d\n", pBestMasternode->vin.prevout.ToStringShort(), nLastPaid);
        pBestMasternode->nTimeLastPaid = GetAdjustedTime();
    }
}

bool CMasternodeMan::IsMasternodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CMasternode* pmn = Find(vin);
    if (!pmn)
        return false;
    return pmn->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CMasternodeMan::UpdatedBlockTip(const CBlockIndex* pindex)
{
    cvLastBlockHashes.Add(pindex->GetBlockHash());
    if (fLiteMode) return;
    if (!fMasternodeMode) return;
    if (!masternodeSync.IsBlockchainSynced()) return;

    static int nTick = 0;
    static int nTickSync = 0;
    static int nTickDseg = 0;

    // nTick: used for masternode ping
    // nTickSync: used for masternode sync
    // nTickDseg: used for masternode dseg update

    nTick++;

    if (nTick % 60 == 0) {
        // check if we should sync
        nTickSync++;
        if (nTickSync >= 10) { // every 10 minutes
            nTickSync = 0;
            masternodeSync.CheckAndUpdateMasternodeStatus();
        }
    }

    if (nTick % 60 == 0) {
        // check if we should sync
        nTickDseg++;
        if (nTickDseg >= 10) { // every 10 minutes
            nTickDseg = 0;
            CheckAndRemove();
        }
    }
}

void CMasternodeMan::NotifyMasternodeUpdates(CConnman& connman)
{
    // Avoid double locking
    bool fMasternodesAdded = false;
    bool fMasternodesRemoved = false;
    {
        LOCK(cs);
        fMasternodesAdded = (nLastMasternodeCount != vMasternodes.size());
        fMasternodesRemoved = (nLastMasternodeCount > vMasternodes.size());
        nLastMasternodeCount = vMasternodes.size();
    }

    if (fMasternodesAdded) {
        governance.CheckMasternodeOrphanObjects(connman);
        governance.CheckMasternodeOrphanVotes(connman);
    }
    if (fMasternodesRemoved) {
        governance.UpdateCachesAndClean();
    }
}

// ... existing code ...