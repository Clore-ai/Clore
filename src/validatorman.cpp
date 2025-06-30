// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activevalidator.h"
#include "addrman.h"
#include "obfuscation.h"
#include "spork.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "util.h"
#include "validator.h"
#include "validatorman.h"
#include <boost/filesystem.hpp>

#define VALIDATORS_DUMP_SECONDS (15 * 60)
#define VALIDATORS_DSEG_SECONDS (3 * 60 * 60)

CValidatorMan mnodeman;

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
    bool operator()(const std::pair<int64_t, CValidator>& t1,
        const std::pair<int64_t, CValidator>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CValidatorDB
//

CValidatorDB::CValidatorDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "ValidatorCache";
}

bool CValidatorDB::Write(const CValidatorMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssValidators(SER_DISK, CLIENT_VERSION);
    ssValidators << strMagicMessage;          // validator cache file specific magic message
    ssValidators << FLATFILE(CLIENT_VERSION); // validator cache file format version
    mnodemanToSave.Serialize(ssValidators);

    uint256 hash = Hash(ssValidators.begin(), ssValidators.end());
    ssValidators << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssValidators;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    FileCommit(fileout.Get());
    fileout.fclose();

    LogPrint(BCLog::VALIDATOR, "Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::VALIDATOR, "  %s\n", mnodemanToSave.ToString());

    return true;
}

CValidatorDB::ReadResult CValidatorDB::Read(CValidatorMan& mnodemanToLoad, bool fDryRun)
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

    CDataStream ssValidators(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssValidators.begin(), ssValidators.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (validator cache file specific magic message) and ..
        ssValidators >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid validator cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssValidators >> FLATFILE(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CValidatorMan object
        ssValidators >> mnodemanToLoad;
    } catch (const std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::VALIDATOR, "Loaded info from mncache.dat (dbversion=%d) %dms\n", mnodemanToLoad.nDbVersion, GetTimeMillis() - nStart);
    LogPrint(BCLog::VALIDATOR, "  %s\n", mnodemanToLoad.ToString());

    return Ok;
}

void DumpValidators()
{
    int64_t nStart = GetTimeMillis();

    CValidatorDB mndb;
    CValidatorMan tempMnodeman;

    LogPrint(BCLog::VALIDATOR, "Verifying mncache.dat format...\n");
    CValidatorDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CValidatorDB::FileError)
        LogPrint(BCLog::VALIDATOR, "Missing validator cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CValidatorDB::Ok) {
        LogPrint(BCLog::VALIDATOR, "Error reading mncache.dat: ");
        if (readResult == CValidatorDB::IncorrectFormat)
            LogPrint(BCLog::VALIDATOR, "magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint(BCLog::VALIDATOR, "file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint(BCLog::VALIDATOR, "Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint(BCLog::VALIDATOR, "Validator dump finished  %dms\n", GetTimeMillis() - nStart);
}

CValidatorMan::CValidatorMan() : cvLastBlockHashes(CACHED_BLOCK_HASHES, UINT256_ZERO)
{
    SetLastPaid();
}

bool CValidatorMan::Add(CValidator& validator)
{
    LOCK(cs);

    if (Has(validator.vin.prevout)) return false;

    LogPrint(BCLog::VALIDATOR, "CValidatorMan::Add -- Adding new Validator: addr=%s, %i now\n", validator.addr.ToString(), size() + 1);
    vValidators.push_back(validator);
    return true;
}

void CValidatorMan::AskForMN(CNode* pnode, const CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForValidatorListEntry.find(vin.prevout);
    if (i != mWeAskedForValidatorListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint(BCLog::VALIDATOR, "CValidatorMan::AskForMN -- Asking node for missing entry, vin: %s\n", vin.prevout.ToStringShort());
    g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::GETMNLIST, vin));
    int64_t askAgain = GetTime() + VALIDATORS_DSEG_SECONDS;
    mWeAskedForValidatorListEntry[vin.prevout] = askAgain;
}

void CValidatorMan::Check()
{
    LOCK(cs);

    for (CValidator& validator : vValidators) {
        validator.Check();
    }
}

void CValidatorMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    // remove inactive and outdated
    auto it = vValidators.begin();
    while (it != vValidators.end()) {
        if ((*it).activeState == CValidator::VALIDATOR_REMOVE ||
            (*it).activeState == CValidator::VALIDATOR_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CValidator::VALIDATOR_EXPIRED) ||
            (*it).protocolVersion < MIN_VALIDATOR_PAYMENT_PROTO_VERSION) {
            LogPrint(BCLog::VALIDATOR, "CValidatorMan::CheckAndRemove -- Removing inactive Validator: %s  %i now\n", (*it).vin.prevout.ToStringShort(), size() - 1);

            // erase all of the broadcasts we've seen from this vin
            //  -- if we missed a few pings and the node was removed, this will allow us to get it back without them
            //     sending a brand new mnb
            std::map<uint256, CValidatorBroadcast>::iterator it3 = mapSeenValidatorBroadcast.begin();
            while (it3 != mapSeenValidatorBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    validatorSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenValidatorBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this validator again if we see another ping
            std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForValidatorListEntry.begin();
            while (it2 != mWeAskedForValidatorListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForValidatorListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vValidators.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Validator list
    std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForValidatorList.begin();
    while (it1 != mAskedUsForValidatorList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForValidatorList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Validator list
    it1 = mWeAskedForValidatorList.begin();
    while (it1 != mWeAskedForValidatorList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForValidatorList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Validators we've asked for
    std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForValidatorListEntry.begin();
    while (it2 != mWeAskedForValidatorListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForValidatorListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenValidatorBroadcast
    std::map<uint256, CValidatorBroadcast>::iterator it3 = mapSeenValidatorBroadcast.begin();
    while (it3 != mapSeenValidatorBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (VALIDATOR_REMOVAL_SECONDS * 2)) {
            mapSeenValidatorBroadcast.erase(it3++);
            validatorSync.mapSeenSyncMNB.erase((*it3).first);
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenValidatorPing
    std::map<uint256, CValidatorPing>::iterator it4 = mapSeenValidatorPing.begin();
    while (it4 != mapSeenValidatorPing.end()) {
        if ((*it4).second.sigTime < GetTime() - (VALIDATOR_REMOVAL_SECONDS * 2)) {
            mapSeenValidatorPing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CValidatorMan::Clear()
{
    LOCK(cs);
    vValidators.clear();
    mAskedUsForValidatorList.clear();
    mWeAskedForValidatorList.clear();
    mWeAskedForValidatorListEntry.clear();
    mapSeenValidatorBroadcast.clear();
    mapSeenValidatorPing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
    indexValidators.Clear();
    indexValidatorsOld.Clear();
}

int CValidatorMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? MIN_VALIDATOR_PAYMENT_PROTO_VERSION : protocolVersion;

    for (const CValidator& validator : vValidators) {
        if (validator.protocolVersion < protocolVersion || !validator.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CValidatorMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? MIN_VALIDATOR_PAYMENT_PROTO_VERSION : protocolVersion;

    for (const CValidator& validator : vValidators) {
        validator.Check();
        std::string strHost;
        int port;
        SplitHostPort(validator.addr.ToString(), port, strHost);
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

void CValidatorMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForValidatorList.find(pnode->addr);
            if (it != mWeAskedForValidatorList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint(BCLog::VALIDATOR, "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::GETMNLIST, CTxIn()));
    int64_t askAgain = GetTime() + VALIDATORS_DSEG_SECONDS;
    mWeAskedForValidatorList[pnode->addr] = askAgain;
}

CValidator* CValidatorMan::Find(const COutPoint& outpoint)
{
    LOCK(cs);
    return indexValidators.Get(outpoint);
}

CValidator* CValidatorMan::Find(const CTxIn& vin)
{
    LOCK(cs);
    return indexValidators.Get(vin.prevout);
}

CValidator* CValidatorMan::Find(const CPubKey& pubKeyValidator)
{
    LOCK(cs);
    for (auto& validator : vValidators) {
        if (validator.pubKeyValidator == pubKeyValidator)
            return &validator;
    }
    return NULL;
}

bool CValidatorMan::Get(const CPubKey& pubKeyValidator, CValidator& validator)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    // However, locking cs_main and cs together is not safe, so we use a temporary mutex.
    // For details see commit 03b9f24fcc1f5721a51f79ee1f48b7d8e7c2adfb
    LOCK(cs);
    CValidator* pmn = Find(pubKeyValidator);
    if (!pmn)
        return false;
    validator = *pmn;
    return true;
}

bool CValidatorMan::Get(const CTxIn& vin, CValidator& validator)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    // However, locking cs_main and cs together is not safe, so we use a temporary mutex.
    // For details see commit 03b9f24fcc1f5721a51f79ee1f48b7d8e7c2adfb
    LOCK(cs);
    CValidator* pmn = Find(vin);
    if (!pmn)
        return false;
    validator = *pmn;
    return true;
}

validator_info_t CValidatorMan::GetValidatorInfo(const CTxIn& vin)
{
    validator_info_t info;
    LOCK(cs);
    CValidator* pmn = Find(vin);
    if (!pmn)
        return info;
    info = pmn->GetInfo();
    return info;
}

validator_info_t CValidatorMan::GetValidatorInfo(const CPubKey& pubKeyValidator)
{
    validator_info_t info;
    LOCK(cs);
    CValidator* pmn = Find(pubKeyValidator);
    if (!pmn)
        return info;
    info = pmn->GetInfo();
    return info;
}

bool CValidatorMan::Has(const CTxIn& vin)
{
    LOCK(cs);
    CValidator* pmn = Find(vin);
    return (pmn != NULL);
}

bool CValidatorMan::Has(const COutPoint& outpoint)
{
    LOCK(cs);
    return indexValidators.Contains(outpoint);
}

validator_info_t CValidator::GetInfo()
{
    validator_info_t info;
    info.vin = vin;
    info.addr = addr;
    info.pubKeyCollateralAddress = pubKeyCollateralAddress;
    info.pubKeyValidator = pubKeyValidator;
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

bool CValidatorMan::IsValidatorPingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CValidator* pmn = Find(vin);
    if (!pmn)
        return false;
    return pmn->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CValidatorMan::SetLastPaid()
{
    LOCK(cs);

    if (vValidators.empty()) {
        return;
    }

    int64_t nLastPaid = 0;
    CValidator* pBestValidator = NULL;

    for (CValidator& validator : vValidators) {
        if (!validator.IsEnabled()) continue;

        // check protocol version
        if (validator.protocolVersion < MIN_VALIDATOR_PAYMENT_PROTO_VERSION) continue;

        // it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (validatorPayments.IsScheduled(validator, 0)) continue;

        // it's too new, wait for a cycle
        if (fFilterSigTime && validator.sigTime + (nValidatorMinimumConfirmations * 2.6 * 60) > GetAdjustedTime()) continue;

        // make sure it has as many confirmations as there are blocks
        if (validator.GetValidatorInputAge() < nValidatorMinimumConfirmations) continue;

        if (validator.nLastPaid > nLastPaid) {
            nLastPaid = validator.nLastPaid;
            pBestValidator = &validator;
        }
    }

    if (pBestValidator) {
        LogPrint(BCLog::VALIDATOR, "CValidatorMan::SetLastPaid -- found: %s with %d\n", pBestValidator->vin.prevout.ToStringShort(), nLastPaid);
        pBestValidator->nTimeLastPaid = GetAdjustedTime();
    }
}

bool CValidatorMan::IsValidatorPingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CValidator* pmn = Find(vin);
    if (!pmn)
        return false;
    return pmn->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CValidatorMan::UpdatedBlockTip(const CBlockIndex* pindex)
{
    cvLastBlockHashes.Add(pindex->GetBlockHash());
    if (fLiteMode) return;
    if (!fValidatorMode) return;
    if (!validatorSync.IsBlockchainSynced()) return;

    static int nTick = 0;
    static int nTickSync = 0;
    static int nTickDseg = 0;

    // nTick: used for validator ping
    // nTickSync: used for validator sync
    // nTickDseg: used for validator dseg update

    nTick++;

    if (nTick % 60 == 0) {
        // check if we should sync
        nTickSync++;
        if (nTickSync >= 10) { // every 10 minutes
            nTickSync = 0;
            validatorSync.CheckAndUpdateValidatorStatus();
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

void CValidatorMan::NotifyValidatorUpdates(CConnman& connman)
{
    // Avoid double locking
    bool fValidatorsAdded = false;
    bool fValidatorsRemoved = false;
    {
        LOCK(cs);
        fValidatorsAdded = (nLastValidatorCount != vValidators.size());
        fValidatorsRemoved = (nLastValidatorCount > vValidators.size());
        nLastValidatorCount = vValidators.size();
    }

    if (fValidatorsAdded) {
        governance.CheckValidatorOrphanObjects(connman);
        governance.CheckValidatorOrphanVotes(connman);
    }
    if (fValidatorsRemoved) {
        governance.UpdateCachesAndClean();
    }
}

// ... existing code ...