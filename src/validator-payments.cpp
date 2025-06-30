// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validator-payments.h"

#include "activevalidator.h"
#include "chainparams.h"
#include "consensus/validation.h"
#include "messagesigner.h"
#include "net.h"
#include "netmessagemaker.h"
#include "spork.h"
#include "stakereward.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "validatorman.h"

#include <boost/filesystem.hpp>

// CLORE: Validator payments are DISABLED by default
// This can be enabled in chainparams or via command line
bool fValidatorPaymentsEnabled = false;

CValidatorPayments validatorPayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapValidatorBlocks;
CCriticalSection cs_mapValidatorPayeeVotes;

//
// CValidatorPaymentDB
//

CValidatorPaymentDB::CValidatorPaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "ValidatorPayments";
}

bool CValidatorPaymentDB::Write(const CValidatorPayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // validator cache file specific magic message
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

    LogPrint(BCLog::VALIDATOR, "Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CValidatorPaymentDB::ReadResult CValidatorPaymentDB::Read(CValidatorPayments& objToLoad)
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
        // de-serialize file header (validator cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid validator payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CValidatorPayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::VALIDATOR, "Loaded info from mnpayments.dat %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::VALIDATOR, "  %s\n", objToLoad.ToString());

    return Ok;
}

uint256 CValidatorPaymentWinner::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vinValidator;
    ss << nBlockHeight;
    ss << payee;
    return ss.GetHash();
}

std::string CValidatorPaymentWinner::GetStrMessage() const
{
    return vinValidator.prevout.ToStringShort() + std::to_string(nBlockHeight) + payee.ToString();
}

bool CValidatorPaymentWinner::IsValid(CNode* pnode, CValidationState& state, int chainHeight)
{
    // CLORE: If payments are disabled, reject all payment votes
    if (!fValidatorPaymentsEnabled) {
        return state.Error("Validator payments are disabled");
    }

    CValidator* pmn = mnodeman.Find(vinValidator.prevout);
    if (!pmn) {
        return state.Error(strprintf("Unknown Validator %s", vinValidator.prevout.ToStringShort()));
    }

    int n = mnodeman.GetValidatorRank(vinValidator, nBlockHeight - 100);
    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        return state.Error(strprintf("Validator not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, n));
    }

    return true;
}

void CValidatorPaymentWinner::Relay()
{
    CInv inv(MSG_VALIDATOR_PAYMENT, GetHash());
    g_connman->RelayInv(inv);
}

void DumpValidatorPayments()
{
    int64_t nStart = GetTimeMillis();

    CValidatorPaymentDB paymentdb;
    LogPrint(BCLog::VALIDATOR, "Writing info to mnpayments.dat...\n");
    paymentdb.Write(validatorPayments);

    LogPrint(BCLog::VALIDATOR, "Validator payment dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    // CLORE: If validator payments are disabled, use standard validation
    if (!fValidatorPaymentsEnabled) {
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

    // Get expected validator payment
    CAmount nExpectedValidatorPayment = GetValidatorPayment(nHeight);
    CAmount nMaxValue = nExpectedValue + nExpectedValidatorPayment;

    return nMinted <= nMaxValue;
}

bool IsBlockPayeeValid(const CTransaction& txNew, const CBlockIndex* pindexPrev)
{
    // CLORE: If payments are disabled, any payee is valid
    if (!fValidatorPaymentsEnabled) {
        return true;
    }

    if (!validatorSync.IsSynced()) {
        // There is no budget data to use to check anything, let's just accept the longest chain
        LogPrint(BCLog::VALIDATOR, "Client not synced, skipping block payee checks\n");
        return true;
    }

    // Check if validator payment is valid
    return validatorPayments.IsTransactionValid(txNew, pindexPrev);
}

void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake)
{
    // CLORE: If payments are disabled, don't modify the transactions
    if (!fValidatorPaymentsEnabled) {
        return;
    }

    validatorPayments.FillBlockPayee(txCoinbase, txCoinstake, pindexPrev, fProofOfStake);
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    // CLORE: If payments are disabled, return empty string
    if (!fValidatorPaymentsEnabled) {
        return "Validator payments disabled";
    }

    return validatorPayments.GetRequiredPaymentsString(nBlockHeight);
}

bool CValidatorBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockSubsidy(nBlockHeight, Params().GetConsensus());

    // Require at least 6 signatures
    for (CValidatorPayee& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (CValidatorPayee& payee : vecPayments) {
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

    LogPrint(BCLog::VALIDATOR, "CValidatorBlockPayees::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(GetValidatorPayment(nBlockHeight)).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CValidatorBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (CValidatorPayee& payee : vecPayments) {
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

bool CValidatorPayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    // CLORE: If payments are disabled, return false
    if (!fValidatorPaymentsEnabled) {
        return false;
    }

    if (mapValidatorBlocks.count(nBlockHeight)) {
        return mapValidatorBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

bool CValidatorPayments::IsTransactionValid(const CTransaction& txNew, const CBlockIndex* pindexPrev)
{
    // CLORE: If payments are disabled, all transactions are valid
    if (!fValidatorPaymentsEnabled) {
        return true;
    }

    int nBlockHeight = pindexPrev->nHeight + 1;

    if (mapValidatorBlocks.count(nBlockHeight)) {
        return mapValidatorBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CValidatorPayments::FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake)
{
    // CLORE: If payments are disabled, don't modify transactions
    if (!fValidatorPaymentsEnabled) {
        return;
    }

    int nBlockHeight = pindexPrev->nHeight + 1;
    CAmount validatorPayment = GetValidatorPayment(nBlockHeight);

    if (validatorPayment == 0) {
        return;
    }

    CMutableTransaction* txToModify = fProofOfStake ? &txCoinstake : &txCoinbase;

    CScript payee;
    if (!GetBlockPayee(nBlockHeight, payee)) {
        // No payee found, create a dummy payment to the first validator
        LogPrint(BCLog::VALIDATOR, "CValidatorPayments::FillBlockPayee - Failed to find validator to pay\n");
        return;
    }

    // Add validator payment to transaction
    txToModify->vout.push_back(CTxOut(validatorPayment, payee));

    // Reduce staker reward by validator payment amount
    if (fProofOfStake && txCoinstake.vout.size() > 1) {
        txCoinstake.vout[1].nValue -= validatorPayment;
    } else if (!fProofOfStake && txCoinbase.vout.size() > 1) {
        txCoinbase.vout[1].nValue -= validatorPayment;
    }

    LogPrint(BCLog::VALIDATOR, "CValidatorPayments::FillBlockPayee - Validator payment %lld to %s\n", validatorPayment, payee.ToString());
}

std::string CValidatorPayments::GetRequiredPaymentsString(int nBlockHeight)
{
    // CLORE: If payments are disabled, return appropriate message
    if (!fValidatorPaymentsEnabled) {
        return "Validator payments disabled";
    }

    if (mapValidatorBlocks.count(nBlockHeight)) {
        return mapValidatorBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CValidatorPayments::IsScheduled(const CValidator& validator, int nNotBlockHeight)
{
    // CLORE: If payments are disabled, no validators are scheduled
    if (!fValidatorPaymentsEnabled) {
        return false;
    }

    LOCK(cs_mapValidatorBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(validator.pubKeyCollateralAddress.GetID());

    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapValidatorBlocks.count(h)) {
            if (mapValidatorBlocks[h].HasPayeeWithVotes(mnpayee, 2)) {
                return true;
            }
        }
    }

    return false;
}

CAmount CValidatorPayments::GetValidatorPayment(int nHeight)
{
    // CLORE: Return 0 if payments are disabled
    if (!fValidatorPaymentsEnabled) {
        return 0;
    }

    // Calculate validator payment as a percentage of block reward
    CAmount blockReward = CStakeReward::GetBlockReward(nHeight);

    // For now, set validator payment to 10% of block reward
    // This can be adjusted based on CLORE's tokenomics
    return blockReward / 10;
}

bool CValidatorPayments::AddWinningValidator(CValidatorPaymentWinner& winner)
{
    // CLORE: If payments are disabled, reject all winners
    if (!fValidatorPaymentsEnabled) {
        return false;
    }

    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, winner.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapValidatorBlocks, cs_mapValidatorPayeeVotes);

        if (mapValidatorPayeeVotes.count(winner.GetHash())) {
            return false;
        }

        mapValidatorPayeeVotes[winner.GetHash()] = winner;

        if (!mapValidatorBlocks.count(winner.nBlockHeight)) {
            CValidatorBlockPayees blockPayees(winner.nBlockHeight);
            mapValidatorBlocks[winner.nBlockHeight] = blockPayees;
        }
    }

    mapValidatorBlocks[winner.nBlockHeight].AddPayee(winner.payee, 1);

    return true;
}

bool CValidatorPayments::ProcessBlock(int nBlockHeight)
{
    // CLORE: If payments are disabled, always return true
    if (!fValidatorPaymentsEnabled) {
        return true;
    }

    if (!fMasterNode) return false;

    // Reference node right now
    int n = mnodeman.GetValidatorRank(activeValidator.vin, nBlockHeight - 100);

    if (n == -1) {
        LogPrint(BCLog::VALIDATOR, "CValidatorPayments::ProcessBlock - Unknown Validator\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint(BCLog::VALIDATOR, "CValidatorPayments::ProcessBlock - Validator not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CValidatorPaymentWinner newWinner(activeValidator.vin);

    LogPrint(BCLog::VALIDATOR, "CValidatorPayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeValidator.vin.prevout.hash.ToString());

    // Pay to the oldest Validator that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CValidator* pmn = mnodeman.GetNextValidatorInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn != NULL) {
        LogPrint(BCLog::VALIDATOR, "CValidatorPayments::ProcessBlock() Found by FindOldestNotInVec \n");

        newWinner.nBlockHeight = nBlockHeight;

        CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
        newWinner.AddPayee(payee);

        CTxIn vin = pmn->vin;
    }

    // Sign message
    if (!newWinner.Sign()) {
        LogPrint(BCLog::VALIDATOR, "CValidatorPayments::ProcessBlock - Failed to sign validator winner\n");
        return false;
    }

    if (!AddWinningValidator(newWinner)) {
        return false;
    }

    newWinner.Relay();
    nLastBlockHeight = nBlockHeight;
    return true;
}

void CValidatorPayments::Sync(CNode* node, int nCountNeeded)
{
    // CLORE: If payments are disabled, don't sync
    if (!fValidatorPaymentsEnabled) {
        return;
    }

    LOCK(cs_mapValidatorPayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CValidatorPaymentWinner>::iterator it = mapValidatorPayeeVotes.begin();
    while (it != mapValidatorPayeeVotes.end()) {
        CValidatorPaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_VALIDATOR_PAYMENT, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    g_connman->PushMessage(node, CNetMsgMaker(node->GetSendVersion()).Make("ssc", VALIDATOR_SYNC_MNW, nInvCount));
}

std::string CValidatorPayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapValidatorPayeeVotes.size() << ", Blocks: " << (int)mapValidatorBlocks.size();

    return info.str();
}

// Global functions

CAmount GetValidatorPayment(int nHeight)
{
    return validatorPayments.GetValidatorPayment(nHeight);
}

bool IsSporkActive(int nSporkID)
{
    // CLORE: For now, return false for all sporks since we don't have spork system
    // This can be implemented later if needed
    return false;
}