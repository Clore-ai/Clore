// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_VALIDATOR_PAYMENTS_H
#define CLORE_VALIDATOR_PAYMENTS_H

#include "key.h"
#include "util.h"
#include "validation.h"
#include "validator.h"

class CValidatorPayments;
class CValidatorPaymentWinner;
class CValidatorBlockPayees;

// CLORE: Validator payments are DISABLED by default
// Set to true in chainparams to enable validator payments
extern bool fValidatorPaymentsEnabled;

static const int MNPAYMENTS_SIGNATURES_REQUIRED = 6;
static const int MNPAYMENTS_SIGNATURES_TOTAL = 10;

// Minimum protocol version for validator payments - SET TO IMPOSSIBLY HIGH VALUE TO DISABLE
// CLORE: Validator payments are permanently disabled - this prevents accidental activation
static const int MIN_VALIDATOR_PAYMENT_PROTO_VERSION = 999999999;

//! minimum peer version accepted by DarkSendPool
static const int MIN_POOL_PEER_PROTO_VERSION = 70103;

extern CValidatorPayments validatorPayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
bool IsBlockPayeeValid(const CTransaction& txNew, const CBlockIndex* pindexPrev);
void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake);
std::string GetRequiredPaymentsString(int nBlockHeight);

class CValidatorPayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CValidatorPayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CValidatorPayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    SERIALIZE_METHODS(CValidatorPayee, obj)
    {
        READWRITE(obj.scriptPubKey, obj.nVotes);
    }
};

// Keep track of votes for payees from validators
class CValidatorBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CValidatorPayee> vecPayments;

    CValidatorBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CValidatorBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        for (CValidatorPayee& payee : vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CValidatorPayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        for (CValidatorPayee& p : vecPayments) {
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

        for (CValidatorPayee& p : vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    SERIALIZE_METHODS(CValidatorBlockPayees, obj)
    {
        READWRITE(obj.nBlockHeight, obj.vecPayments);
    }

private:
    mutable CCriticalSection cs_vecPayments;
};

// for storing the winning payments
class CValidatorPaymentWinner : public CSignedMessage
{
public:
    CTxIn vinValidator;

    int nBlockHeight;
    CScript payee;

    CValidatorPaymentWinner()
    {
        nBlockHeight = 0;
        vinValidator = CTxIn();
        payee = CScript();
    }

    CValidatorPaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinValidator = vinIn;
        payee = CScript();
    }

    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const { return vinValidator; };

    bool IsValid(CNode* pnode, CValidationState& state, int chainHeight);
    void Relay();

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }

    SERIALIZE_METHODS(CValidatorPaymentWinner, obj)
    {
        READWRITE(obj.vinValidator, obj.nBlockHeight, obj.payee, obj.vchSig, obj.nMessVersion);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinValidator.ToString();
        ret += ", " + std::to_string(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + std::to_string((int)vchSig.size());
        return ret;
    }
};

//
// Validator Payments Class
// Keeps track of who should get paid for which blocks
//

class CValidatorPayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CValidatorPaymentWinner> mapValidatorPayeeVotes;
    std::map<int, CValidatorBlockPayees> mapValidatorBlocks;
    std::map<CTxIn, int> mapValidatorsLastVote; // prevout.hash + prevout.n, nBlockHeight

    CValidatorPayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapValidatorBlocks, cs_mapValidatorPayeeVotes);
        mapValidatorBlocks.clear();
        mapValidatorPayeeVotes.clear();
    }

    bool AddWinningValidator(CValidatorPaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList(int mnCount, int nHeight);
    int LastCleanPaymentBlock();

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, const CBlockIndex* pindexPrev);
    bool IsScheduled(const CValidator& validator, int nNotBlockHeight);

    bool CanVote(CTxIn vinValidator, int nBlockHeight)
    {
        LOCK(cs_mapValidatorPayeeVotes);

        if (mapValidatorsLastVote.count(vinValidator)) {
            if (mapValidatorsLastVote[vinValidator] == nBlockHeight) {
                return false;
            }
        }

        // record this validator voted
        mapValidatorsLastVote[vinValidator] = nBlockHeight;
        return true;
    }

    int GetMinValidatorPaymentsProto();
    void ProcessMessageValidatorPayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    // CLORE: Get validator payment amount (returns 0 if payments disabled)
    CAmount GetValidatorPayment(int nHeight);

    // CLORE: Check if validator payments are enabled
    bool IsEnabled() const { return fValidatorPaymentsEnabled; }

    SERIALIZE_METHODS(CValidatorPayments, obj)
    {
        READWRITE(obj.mapValidatorPayeeVotes, obj.mapValidatorBlocks, obj.mapValidatorsLastVote);
    }

private:
    mutable CCriticalSection cs_mapValidatorBlocks;
    mutable CCriticalSection cs_mapValidatorPayeeVotes;
};

class CValidatorPaymentDB
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

    CValidatorPaymentDB();
    bool Write(const CValidatorPayments& objToSave);
    ReadResult Read(CValidatorPayments& objToLoad);
};

// Get validator payment amount for a given height
CAmount GetValidatorPayment(int nHeight);

// Check if validator payments are enforced
bool IsSporkActive(int nSporkID);

// Dump validator payments to disk
void DumpValidatorPayments();

#endif // CLORE_VALIDATOR_PAYMENTS_H