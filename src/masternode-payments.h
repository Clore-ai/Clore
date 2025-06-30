// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_MASTERNODE_PAYMENTS_H
#define CLORE_MASTERNODE_PAYMENTS_H

#include "key.h"
#include "masternode.h"
#include "util.h"
#include "validation.h"

class CMasternodePayments;
class CMasternodePaymentWinner;
class CMasternodeBlockPayees;

// CLORE: Masternode payments are DISABLED by default
// Set to true in chainparams to enable masternode payments
extern bool fMasternodePaymentsEnabled;

static const int MNPAYMENTS_SIGNATURES_REQUIRED = 6;
static const int MNPAYMENTS_SIGNATURES_TOTAL = 10;

// Minimum protocol version for masternode payments
static const int MIN_MASTERNODE_PAYMENT_PROTO_VERSION = 70922;

//! minimum peer version accepted by DarkSendPool
static const int MIN_POOL_PEER_PROTO_VERSION = 70103;

extern CMasternodePayments masternodePayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
bool IsBlockPayeeValid(const CTransaction& txNew, const CBlockIndex* pindexPrev);
void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake);
std::string GetRequiredPaymentsString(int nBlockHeight);

class CMasternodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CMasternodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CMasternodePayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    SERIALIZE_METHODS(CMasternodePayee, obj)
    {
        READWRITE(obj.scriptPubKey, obj.nVotes);
    }
};

// Keep track of votes for payees from masternodes
class CMasternodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CMasternodePayee> vecPayments;

    CMasternodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CMasternodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        for (CMasternodePayee& payee : vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CMasternodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        for (CMasternodePayee& p : vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        for (CMasternodePayee& p : vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    SERIALIZE_METHODS(CMasternodeBlockPayees, obj)
    {
        READWRITE(obj.nBlockHeight, obj.vecPayments);
    }

private:
    mutable CCriticalSection cs_vecPayments;
};

// for storing the winning payments
class CMasternodePaymentWinner : public CSignedMessage
{
public:
    CTxIn vinMasternode;

    int nBlockHeight;
    CScript payee;

    CMasternodePaymentWinner()
    {
        nBlockHeight = 0;
        vinMasternode = CTxIn();
        payee = CScript();
    }

    CMasternodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinMasternode = vinIn;
        payee = CScript();
    }

    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const { return vinMasternode; };

    bool IsValid(CNode* pnode, CValidationState& state, int chainHeight);
    void Relay();

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }

    SERIALIZE_METHODS(CMasternodePaymentWinner, obj)
    {
        READWRITE(obj.vinMasternode, obj.nBlockHeight, obj.payee, obj.vchSig, obj.nMessVersion);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinMasternode.ToString();
        ret += ", " + std::to_string(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + std::to_string((int)vchSig.size());
        return ret;
    }
};

//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CMasternodePaymentWinner> mapMasternodePayeeVotes;
    std::map<int, CMasternodeBlockPayees> mapMasternodeBlocks;
    std::map<CTxIn, int> mapMasternodesLastVote; // prevout.hash + prevout.n, nBlockHeight

    CMasternodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);
        mapMasternodeBlocks.clear();
        mapMasternodePayeeVotes.clear();
    }

    bool AddWinningMasternode(CMasternodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList(int mnCount, int nHeight);
    int LastCleanPaymentBlock();

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, const CBlockIndex* pindexPrev);
    bool IsScheduled(const CMasternode& mn, int nNotBlockHeight);

    bool CanVote(CTxIn vinMasternode, int nBlockHeight)
    {
        LOCK(cs_mapMasternodePayeeVotes);

        if (mapMasternodesLastVote.count(vinMasternode)) {
            if (mapMasternodesLastVote[vinMasternode] == nBlockHeight) {
                return false;
            }
        }

        // record this masternode voted
        mapMasternodesLastVote[vinMasternode] = nBlockHeight;
        return true;
    }

    int GetMinMasternodePaymentsProto();
    void ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    // CLORE: Get masternode payment amount (returns 0 if payments disabled)
    CAmount GetMasternodePayment(int nHeight);

    // CLORE: Check if masternode payments are enabled
    bool IsEnabled() const { return fMasternodePaymentsEnabled; }

    SERIALIZE_METHODS(CMasternodePayments, obj)
    {
        READWRITE(obj.mapMasternodePayeeVotes, obj.mapMasternodeBlocks, obj.mapMasternodesLastVote);
    }

private:
    mutable CCriticalSection cs_mapMasternodeBlocks;
    mutable CCriticalSection cs_mapMasternodePayeeVotes;
};

class CMasternodePaymentDB
{
private:
    boost::filesystem::path pathDB;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CMasternodePaymentDB();
    bool Write(const CMasternodePayments& objToSave);
    ReadResult Read(CMasternodePayments& objToLoad);
};

// Get masternode payment amount for a given height
CAmount GetMasternodePayment(int nHeight);

// Check if masternode payments are enforced
bool IsSporkActive(int nSporkID);

// Dump masternode payments to disk
void DumpMasternodePayments();

#endif // CLORE_MASTERNODE_PAYMENTS_H