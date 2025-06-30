// Copyright (c) 2017-2021 The PIVX developers
// Copyright (c) 2024 The CLORE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakeinput.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "hash.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "script/sign.h"
#include "script/standard.h"
#include "stakeconsensus.h"
#include "txdb.h"
#include "util.h"
#include "validation.h"
#include "wallet/wallet.h"

typedef std::vector<unsigned char> valtype;

// Constructor implementation
CCloreStake::CCloreStake(const CTxOut& txOut, const COutPoint& outPoint, const CBlockIndex* pindex)
    : txFrom(nullptr), nPosition(outPoint.n), pindexFrom(pindex)
{
    // Constructor body can be empty for now
}

CCloreStake* CCloreStake::NewCloreStake(const CTxIn& txin, int nHeight, uint32_t nTime)
{
    if (txin.prevout.IsNull()) {
        LogPrintf("%s : null prevout\n", __func__);
        return nullptr;
    }

    // Look for the transaction in the blockchain
    CTransactionRef txPrev;
    uint256 hashBlock;
    if (!GetTransaction(txin.prevout.hash, txPrev, GetParams().GetConsensus(), hashBlock, true)) {
        LogPrintf("%s : failed to find tx %s\n", __func__, txin.prevout.hash.ToString());
        return nullptr;
    }

    // Check that the input exists
    if (txin.prevout.n >= txPrev->vout.size()) {
        LogPrintf("%s : invalid input %s-%d\n", __func__, txin.prevout.hash.ToString(), txin.prevout.n);
        return nullptr;
    }

    // Find the block index
    if (mapBlockIndex.count(hashBlock) == 0) {
        LogPrintf("%s : failed to find block %s\n", __func__, hashBlock.ToString());
        return nullptr;
    }

    const CBlockIndex* pindexFrom = mapBlockIndex.at(hashBlock);
    if (!pindexFrom || !chainActive.Contains(pindexFrom)) {
        LogPrintf("%s : block %s not in active chain\n", __func__, hashBlock.ToString());
        return nullptr;
    }

    return new CCloreStake(txPrev->vout[txin.prevout.n], txin.prevout, pindexFrom);
}

bool CCloreStake::SetInput(const CTransaction* txPrev, unsigned int n)
{
    if (!txPrev || n >= txPrev->vout.size()) {
        return false;
    }

    this->txFrom = txPrev;
    this->nPosition = n;

    // Find the block index for this transaction
    uint256 hashBlock;
    CTransactionRef tempTxRef;
    if (!GetTransaction(txPrev->GetHash(), tempTxRef, GetParams().GetConsensus(), hashBlock, true)) {
        return false;
    }

    if (hashBlock.IsNull()) {
        return false;
    }

    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end()) {
        return false;
    }

    this->pindexFrom = mi->second;
    return true;
}

const CBlockIndex* CCloreStake::GetIndexFrom() const
{
    return pindexFrom;
}

bool CCloreStake::GetTxOutFrom(CTxOut& out) const
{
    if (!txFrom || nPosition >= txFrom->vout.size()) {
        return false;
    }

    out = txFrom->vout[nPosition];
    return true;
}

CAmount CCloreStake::GetValue() const
{
    if (!txFrom || nPosition >= txFrom->vout.size()) {
        return 0;
    }

    return txFrom->vout[nPosition].nValue;
}

CDataStream CCloreStake::GetUniqueness() const
{
    CDataStream ss(SER_GETHASH, 0);
    if (txFrom) {
        ss << txFrom->GetHash() << nPosition;
    }
    return ss;
}

bool CCloreStake::CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut)
{
    if (!txFrom) {
        return false;
    }

    txIn = CTxIn(txFrom->GetHash(), nPosition);
    return true;
}

bool CCloreStake::CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal)
{
    if (!txFrom || nPosition >= txFrom->vout.size()) {
        return false;
    }

    std::vector<valtype> vSolutions;
    txnouttype whichType;
    CScript scriptPubKeyKernel = txFrom->vout[nPosition].scriptPubKey;
    if (!Solver(scriptPubKeyKernel, whichType, vSolutions)) {
        LogPrintf("%s : failed to parse kernel\n", __func__);
        return false;
    }

    if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH) {
        LogPrintf("%s : type=%d (%s) not supported for scriptPubKeyKernel\n", __func__, whichType, GetTxnOutputType(whichType));
        return false;
    }

    CScript scriptPubKey;
    if (whichType == TX_PUBKEYHASH) {
        // if P2PKH check that we have the input private key
        if (!pwallet) {
            LogPrintf("%s : wallet not available\n", __func__);
            return false;
        }

        CKeyID keyID = CKeyID(uint160(vSolutions[0]));
        CKey key;
        if (!pwallet->GetKey(keyID, key)) {
            LogPrintf("%s : failed to get key for kernel type=%d\n", __func__, whichType);
            return false;
        }
        scriptPubKey = scriptPubKeyKernel;
    } else {
        // if P2PK, use the same script
        scriptPubKey = scriptPubKeyKernel;
    }

    vout.emplace_back(CTxOut(0, scriptPubKey));

    // For now, just create a single output (stake splitting can be added later)
    // Future: implement stake splitting based on wallet configuration

    return true;
}

bool CCloreStake::GetModifier(uint64_t& nStakeModifier) const
{
    if (!pindexFrom) {
        return false;
    }

    // Calculate stake modifier from block hash (simplified implementation)
    // In full implementation, this would use proper stake modifier calculation
    CDataStream ss(SER_GETHASH, 0);
    ss << pindexFrom->GetBlockHash();
    uint256 hashModifier = Hash(ss.begin(), ss.end());
    nStakeModifier = hashModifier.GetUint64(0);
    return true;
}

bool CCloreStake::ContextCheck(int nHeight, uint32_t nTime)
{
    if (!pindexFrom) {
        return false;
    }

    // Check that we're not spending a stake too young
    const Consensus::Params& params = GetParams().GetConsensus();
    int nStakeMinAge = params.nStakeMinAge;

    // Check minimum age
    if (nTime < pindexFrom->nTime + nStakeMinAge) {
        return false;
    }

    // Check depth requirements
    int nRequiredDepth = params.nStakeMinDepth;
    if (nHeight - pindexFrom->nHeight < nRequiredDepth) {
        return false;
    }

    return true;
}

bool CCloreStake::IsValid() const
{
    return (pindexFrom && txFrom && nPosition < txFrom->vout.size());
}

CTxIn CCloreStake::GetTxIn() const
{
    if (!txFrom) {
        return CTxIn();
    }
    return CTxIn(txFrom->GetHash(), nPosition);
}

bool CCloreStake::GetStakeKernelHash(uint256& hashRet) const
{
    if (!pindexFrom) {
        return false;
    }

    // Get the stake modifier
    uint64_t nStakeModifier = 0;
    if (!GetModifier(nStakeModifier)) {
        return false;
    }

    // Get the stake uniqueness
    CDataStream ssUniqueness = GetUniqueness();

    // Get the stake value
    CAmount stakeValue = GetValue();

    // Get the stake time
    int64_t nTimeBlockFrom = pindexFrom->GetBlockTime();

    // Create the kernel hash
    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier;
    ss << nTimeBlockFrom;
    // Append uniqueness data from ssUniqueness
    ss << ssUniqueness;
    ss << stakeValue;
    hashRet = Hash(ss.begin(), ss.end());

    return true;
}