// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"

#include "activemasternode.h"
#include "chainparams.h"
#include "consensus/validation.h"
#include "masternodeman.h"
#include "messagesigner.h"
#include "net.h"
#include "netmessagemaker.h"
#include "spork.h"
#include "stakereward.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation.h"

#include <boost/filesystem.hpp>

// CLORE: Masternode payments are DISABLED by default
// This can be enabled in chainparams or via command line
bool fMasternodePaymentsEnabled = false;

CMasternodePayments masternodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapMasternodeBlocks;
CCriticalSection cs_mapMasternodePayeeVotes;

//
// CMasternodePaymentDB
//

CMasternodePaymentDB::CMasternodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "MasternodePayments";
}

bool CMasternodePaymentDB::Write(const CMasternodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // masternode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint(BCLog::MASTERNODE, "Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CMasternodePaymentDB::ReadResult CMasternodePaymentDB::Read(CMasternodePayments& objToLoad)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
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
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CMasternodePayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::MASTERNODE, "Loaded info from mnpayments.dat %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::MASTERNODE, "  %s\n", objToLoad.ToString());

    return Ok;
}

uint256 CMasternodePaymentWinner::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vinMasternode;
    ss << nBlockHeight;
    ss << payee;
    return ss.GetHash();
}

std::string CMasternodePaymentWinner::GetStrMessage() const
{
    return vinMasternode.prevout.ToStringShort() + std::to_string(nBlockHeight) + payee.ToString();
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, CValidationState& state, int chainHeight)
{
    // CLORE: If payments are disabled, reject all payment votes
    if (!fMasternodePaymentsEnabled) {
        return state.Error("Masternode payments are disabled");
    }

    CMasternode* pmn = mnodeman.Find(vinMasternode.prevout);
    if (!pmn) {
        return state.Error(strprintf("Unknown Masternode %s", vinMasternode.prevout.ToStringShort()));
    }

    int n = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 100);
    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        return state.Error(strprintf("Masternode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, n));
    }

    return true;
}

void CMasternodePaymentWinner::Relay()
{
    CInv inv(MSG_MASTERNODE_PAYMENT, GetHash());
    g_connman->RelayInv(inv);
}

void DumpMasternodePayments()
{
    int64_t nStart = GetTimeMillis();

    CMasternodePaymentDB paymentdb;
    LogPrint(BCLog::MASTERNODE, "Writing info to mnpayments.dat...\n");
    paymentdb.Write(masternodePayments);

    LogPrint(BCLog::MASTERNODE, "Masternode payment dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    // CLORE: If masternode payments are disabled, use standard validation
    if (!fMasternodePaymentsEnabled) {
        return nMinted <= nExpectedValue;
    }

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else {
        // This function should never be called with a block that doesn't build on the current tip
        return false;
    }

    // Get expected masternode payment
    CAmount nExpectedMasternodePayment = GetMasternodePayment(nHeight);
    CAmount nMaxValue = nExpectedValue + nExpectedMasternodePayment;

    return nMinted <= nMaxValue;
}

bool IsBlockPayeeValid(const CTransaction& txNew, const CBlockIndex* pindexPrev)
{
    // CLORE: If payments are disabled, any payee is valid
    if (!fMasternodePaymentsEnabled) {
        return true;
    }

    if (!masternodeSync.IsSynced()) {
        // There is no budget data to use to check anything, let's just accept the longest chain
        LogPrint(BCLog::MASTERNODE, "Client not synced, skipping block payee checks\n");
        return true;
    }

    // Check if masternode payment is valid
    return masternodePayments.IsTransactionValid(txNew, pindexPrev);
}

void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake)
{
    // CLORE: If payments are disabled, don't modify the transactions
    if (!fMasternodePaymentsEnabled) {
        return;
    }

    masternodePayments.FillBlockPayee(txCoinbase, txCoinstake, pindexPrev, fProofOfStake);
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    // CLORE: If payments are disabled, return empty string
    if (!fMasternodePaymentsEnabled) {
        return "Masternode payments disabled";
    }

    return masternodePayments.GetRequiredPaymentsString(nBlockHeight);
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockSubsidy(nBlockHeight, Params().GetConsensus());

    // Require at least 6 signatures
    for (CMasternodePayee& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (CMasternodePayee& payee : vecPayments) {
        bool found = false;
        for (CTxOut out : txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                found = true;
                break;
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrint(BCLog::MASTERNODE, "CMasternodeBlockPayees::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(GetMasternodePayment(nBlockHeight)).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (CMasternodePayee& payee : vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if (ret != "Unknown") {
            ret += ", " + address2.ToString() + ":" + std::to_string(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + std::to_string(payee.nVotes);
        }
    }

    return ret;
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    // CLORE: If payments are disabled, return false
    if (!fMasternodePaymentsEnabled) {
        return false;
    }

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, const CBlockIndex* pindexPrev)
{
    // CLORE: If payments are disabled, all transactions are valid
    if (!fMasternodePaymentsEnabled) {
        return true;
    }

    int nBlockHeight = pindexPrev->nHeight + 1;

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake)
{
    // CLORE: If payments are disabled, don't modify transactions
    if (!fMasternodePaymentsEnabled) {
        return;
    }

    int nBlockHeight = pindexPrev->nHeight + 1;
    CAmount masternodePayment = GetMasternodePayment(nBlockHeight);

    if (masternodePayment == 0) {
        return;
    }

    CMutableTransaction* txToModify = fProofOfStake ? &txCoinstake : &txCoinbase;

    CScript payee;
    if (!GetBlockPayee(nBlockHeight, payee)) {
        // No payee found, create a dummy payment to the first masternode
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::FillBlockPayee - Failed to find masternode to pay\n");
        return;
    }

    // Add masternode payment to transaction
    txToModify->vout.push_back(CTxOut(masternodePayment, payee));

    // Reduce staker reward by masternode payment amount
    if (fProofOfStake && txCoinstake.vout.size() > 1) {
        txCoinstake.vout[1].nValue -= masternodePayment;
    } else if (!fProofOfStake && txCoinbase.vout.size() > 1) {
        txCoinbase.vout[1].nValue -= masternodePayment;
    }

    LogPrint(BCLog::MASTERNODE, "CMasternodePayments::FillBlockPayee - Masternode payment %lld to %s\n", masternodePayment, payee.ToString());
}

std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    // CLORE: If payments are disabled, return appropriate message
    if (!fMasternodePaymentsEnabled) {
        return "Masternode payments disabled";
    }

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMasternodePayments::IsScheduled(const CMasternode& mn, int nNotBlockHeight)
{
    // CLORE: If payments are disabled, no masternodes are scheduled
    if (!fMasternodePaymentsEnabled) {
        return false;
    }

    LOCK(cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapMasternodeBlocks.count(h)) {
            if (mapMasternodeBlocks[h].HasPayeeWithVotes(mnpayee, 2)) {
                return true;
            }
        }
    }

    return false;
}

CAmount CMasternodePayments::GetMasternodePayment(int nHeight)
{
    // CLORE: Return 0 if payments are disabled
    if (!fMasternodePaymentsEnabled) {
        return 0;
    }

    // Calculate masternode payment as a percentage of block reward
    CAmount blockReward = CStakeReward::GetBlockReward(nHeight);

    // For now, set masternode payment to 10% of block reward
    // This can be adjusted based on CLORE's tokenomics
    return blockReward / 10;
}

bool CMasternodePayments::AddWinningMasternode(CMasternodePaymentWinner& winner)
{
    // CLORE: If payments are disabled, reject all winners
    if (!fMasternodePaymentsEnabled) {
        return false;
    }

    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, winner.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);

        if (mapMasternodePayeeVotes.count(winner.GetHash())) {
            return false;
        }

        mapMasternodePayeeVotes[winner.GetHash()] = winner;

        if (!mapMasternodeBlocks.count(winner.nBlockHeight)) {
            CMasternodeBlockPayees blockPayees(winner.nBlockHeight);
            mapMasternodeBlocks[winner.nBlockHeight] = blockPayees;
        }
    }

    mapMasternodeBlocks[winner.nBlockHeight].AddPayee(winner.payee, 1);

    return true;
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight)
{
    // CLORE: If payments are disabled, always return true
    if (!fMasternodePaymentsEnabled) {
        return true;
    }

    if (!fMasterNode) return false;

    // Reference node right now
    int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight - 100);

    if (n == -1) {
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock - Unknown Masternode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CMasternodePaymentWinner newWinner(activeMasternode.vin);

    LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin.prevout.hash.ToString());

    // Pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn != NULL) {
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

        newWinner.nBlockHeight = nBlockHeight;

        CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
        newWinner.AddPayee(payee);

        CTxIn vin = pmn->vin;
    }

    // Sign message
    if (!newWinner.Sign()) {
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock - Failed to sign masternode winner\n");
        return false;
    }

    if (!AddWinningMasternode(newWinner)) {
        return false;
    }

    newWinner.Relay();
    nLastBlockHeight = nBlockHeight;
    return true;
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded)
{
    // CLORE: If payments are disabled, don't sync
    if (!fMasternodePaymentsEnabled) {
        return;
    }

    LOCK(cs_mapMasternodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_MASTERNODE_PAYMENT, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    g_connman->PushMessage(node, CNetMsgMaker(node->GetSendVersion()).Make("ssc", MASTERNODE_SYNC_MNW, nInvCount));
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}

// Global functions

CAmount GetMasternodePayment(int nHeight)
{
    return masternodePayments.GetMasternodePayment(nHeight);
}

bool IsSporkActive(int nSporkID)
{
    // CLORE: For now, return false for all sporks since we don't have spork system
    // This can be implemented later if needed
    return false;
}