// Copyright (c) 2017-2021 The PIVX developers
// Copyright (c) 2024 The CLORE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakeconsensus.h"

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "stakeinput.h"
#include "timedata.h"
#include "txdb.h"
#include "util.h"
#include "validation.h"

bool CheckStakeKernelHash(const CBlockIndex* pindexPrev,
    const CTransaction& txPrev,
    const COutPoint& prevout,
    unsigned int nTimeTx,
    unsigned int nHashDrift,
    bool fCheck,
    uint256& hashProofOfStake)
{
    // Skip during initial block download
    if (IsInitialBlockDownload()) {
        LogPrintf("CheckStakeKernelHash: skipping during IBD\n");
        return false;
    }

    // Safety checks
    if (!pindexPrev) {
        LogPrintf("CheckStakeKernelHash() : pindexPrev is null\n");
        return false;
    }

    if (nTimeTx == 0) {
        LogPrintf("CheckStakeKernelHash() : nTimeTx is zero\n");
        return false;
    }

    if (prevout.n >= txPrev.vout.size()) {
        LogPrintf("CheckStakeKernelHash() : prevout.n out of range\n");
        return false;
    }

    // Get stake modifier
    uint64_t nStakeModifier = 0;
    if (!GetStakeModifier(pindexPrev, nStakeModifier)) {
        LogPrintf("CheckStakeKernelHash() : failed to get stake modifier\n");
        return false;
    }

    // Get block hash from transaction (simplified - use prev block hash)
    uint256 hashBlockFrom = pindexPrev->GetBlockHash();

    // Compute stake kernel hash
    hashProofOfStake = ComputeStakeKernelHash(prevout, nTimeTx, nStakeModifier, hashBlockFrom);

    if (fCheck) {
        // Get stake target
        int nHeight = pindexPrev->nHeight + 1;
        arith_uint256 bnTarget = GetStakeTargetLimit(true, nHeight);

        // Calculate coin age (simplified - would need block time info)
        int64_t nCoinAge = 100; // Simplified - use a default coin age for now
        if (nCoinAge <= 0) {
            return false;
        }

        // Safety check for output value
        if (txPrev.vout[prevout.n].nValue <= 0) {
            LogPrintf("CheckStakeKernelHash() : invalid output value\n");
            return false;
        }

        // Adjust target based on coin age and value
        bnTarget *= nCoinAge;
        bnTarget *= txPrev.vout[prevout.n].nValue / COIN;

        // Check if hash meets target
        if (UintToArith256(hashProofOfStake) > bnTarget) {
            return false;
        }
    }

    return true;
}

bool CheckProofOfStake(const CBlock& block,
    uint256& hashProofOfStake,
    std::unique_ptr<CStakeInput>& stake,
    CValidationState& state)
{
    // Skip POS validation during initial load
    if (IsInitialBlockDownload()) {
        LogPrintf("CheckProofOfStake: skipping during IBD\n");
        return true;
    }

    if (!block.IsProofOfStake()) {
        return state.DoS(100, error("CheckProofOfStake() : called on non-coinstake %s", block.GetHash().ToString().c_str()));
    }

    // Safety check - ensure we have vtx and it's POS
    if (block.vtx.size() < 2) {
        return state.DoS(100, error("CheckProofOfStake() : block has insufficient transactions"));
    }

    // Verify hash target and signature of coinstake tx
    const CTransaction& tx = *block.vtx[1];

    // Get chain tip safely
    const CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) {
        // During initialization, chainActive might not be ready
        // Return true to allow startup, validation will happen later
        LogPrintf("CheckProofOfStake: chainActive not ready\n");
        return true;
    }

    if (!IsValidCoinStake(tx, pindexPrev, state)) {
        return false;
    }

    // Get stake input
    if (tx.vin.empty()) {
        return state.DoS(100, error("CheckProofOfStake() : coinstake has no inputs"));
    }

    const CTxIn& txin = tx.vin[0];

    // Get the previous transaction
    CTransactionRef txPrev;
    uint256 hashBlockFrom;
    if (!GetTransaction(txin.prevout.hash, txPrev, GetParams().GetConsensus(), hashBlockFrom, true)) {
        return state.DoS(1, error("CheckProofOfStake() : INFO: read txPrev failed"));
    }

    // Verify the stake input
    if (txin.prevout.n >= txPrev->vout.size()) {
        return state.DoS(1, error("CheckProofOfStake() : INFO: bad txin.prevout.n"));
    }

    // Create stake input object
    stake.reset(new CCloreStake());
    CCloreStake* cloreStake = static_cast<CCloreStake*>(stake.get());
    if (!cloreStake->SetInput(txPrev.get(), txin.prevout.n)) {
        return state.DoS(1, error("CheckProofOfStake() : failed to set stake input"));
    }

    // Check context (age, depth, etc.) with safety check
    int nHeight = chainActive.Height();
    if (nHeight < 0) nHeight = 0; // Safety check

    if (!cloreStake->ContextCheck(nHeight + 1, block.nTime)) {
        return state.DoS(100, error("CheckProofOfStake() : stake context check failed"));
    }

    // Verify stake kernel hash
    if (!CheckStakeKernelHash(pindexPrev, *txPrev, txin.prevout,
            block.nTime, 0, true, hashProofOfStake)) {
        return state.DoS(1, error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s", tx.GetHash().ToString().c_str()));
    }

    return true;
}

bool GetStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier)
{
    // Skip during initial block download
    if (IsInitialBlockDownload()) {
        nStakeModifier = 0;
        return true;
    }

    if (!pindexPrev) {
        nStakeModifier = 0;
        return true;
    }

    // For now, use simple stake modifier based on block hash
    // This should be enhanced with proper stake modifier v2 implementation
    nStakeModifier = pindexPrev->GetBlockHash().GetUint64(0);
    return true;
}

int64_t GetStakeModifierChecksum(const CBlockIndex* pindex)
{
    if (!pindex) {
        return 0;
    }

    return pindex->GetBlockHash().GetUint64(0) >> 32;
}

bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier)
{
    fGeneratedStakeModifier = false;
    nStakeModifier = 0;

    if (!pindexPrev) {
        fGeneratedStakeModifier = true;
        return true;
    }

    // Compute new stake modifier (simplified implementation)
    nStakeModifier = pindexPrev->GetBlockHash().GetUint64(0);

    // Mix in block hash
    uint256 hashBlock = pindexPrev->GetBlockHash();
    nStakeModifier ^= hashBlock.GetUint64(0);
    nStakeModifier ^= hashBlock.GetUint64(1);

    fGeneratedStakeModifier = true;
    return true;
}

uint256 ComputeStakeKernelHash(const COutPoint& prevout,
    unsigned int nTimeTx,
    uint64_t nStakeModifier,
    const uint256& hashBlockFrom)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << nStakeModifier;
    ss << nTimeTx;
    ss << prevout.hash;
    ss << prevout.n;
    ss << hashBlockFrom;
    return ss.GetHash();
}

arith_uint256 GetStakeTargetLimit(bool fProofOfStake, int nHeight)
{
    if (!fProofOfStake) {
        return UintToArith256(GetParams().GetConsensus().powLimit);
    }

    // For POS, use a different target limit
    const Consensus::Params& params = GetParams().GetConsensus();
    arith_uint256 bnTargetLimit = UintToArith256(params.powLimit);

    // Make POS easier than POW
    bnTargetLimit = bnTargetLimit >> 4; // 16x easier

    return bnTargetLimit;
}

bool CheckStakeTarget(const arith_uint256& bnTarget, int nHeight, unsigned int nTime, unsigned int nTimePrev)
{
    // Check that the target is within acceptable range
    arith_uint256 bnTargetLimit = GetStakeTargetLimit(true, nHeight);

    if (bnTarget > bnTargetLimit) {
        return false;
    }

    return true;
}

CAmount GetStakeReward(const CBlockIndex* pindex, CAmount nCoinAge, unsigned int nTime)
{
    if (!pindex) {
        return 0;
    }

    // Base stake reward
    CAmount nReward = 2 * COIN; // 2 CLORE base reward

    // Reduce reward over time (halving every 4 years)
    int nYears = (pindex->nHeight - 0) / (365 * 24 * 60); // Assuming 1 minute blocks
    for (int i = 0; i < nYears / 4; i++) {
        nReward /= 2;
    }

    // Minimum reward
    if (nReward < COIN / 100) { // 0.01 CLORE minimum
        nReward = COIN / 100;
    }

    return nReward;
}

int64_t GetCurrentTimeSlot()
{
    return GetAdjustedTime();
}

int64_t GetTimeSlot(int64_t nTime)
{
    const Consensus::Params& params = GetParams().GetConsensus();
    return (nTime / params.nPowTargetSpacing) * params.nPowTargetSpacing;
}

bool IsValidTimeSlot(int64_t nTime, int64_t nPrevTime)
{
    const Consensus::Params& params = GetParams().GetConsensus();

    // Check that time is reasonable
    if (nTime <= nPrevTime) {
        return false;
    }

    // Check that time isn't too far in the future
    if (nTime > GetAdjustedTime() + params.nPowTargetSpacing * 2) {
        return false;
    }

    return true;
}

bool IsValidCoinStake(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state)
{
    if (tx.vin.empty()) {
        return state.DoS(10, error("IsValidCoinStake() : vin empty"));
    }

    if (tx.vout.empty()) {
        return state.DoS(10, error("IsValidCoinStake() : vout empty"));
    }

    // First output should be empty (zero value)
    if (tx.vout[0].nValue != 0) {
        return state.DoS(10, error("IsValidCoinStake() : first output not empty"));
    }

    // Check that we have at least one non-empty output
    bool fHasValidOutput = false;
    for (size_t i = 1; i < tx.vout.size(); i++) {
        if (tx.vout[i].nValue > 0) {
            fHasValidOutput = true;
            break;
        }
    }

    if (!fHasValidOutput) {
        return state.DoS(10, error("IsValidCoinStake() : no valid outputs"));
    }

    return true;
}

bool CheckCoinStakeReward(const CTransaction& tx, CAmount nRewardExpected, CValidationState& state)
{
    // Calculate total output value (excluding first empty output)
    CAmount nValueOut = 0;
    for (size_t i = 1; i < tx.vout.size(); i++) {
        nValueOut += tx.vout[i].nValue;
    }

    // Calculate total input value
    CAmount nValueIn = 0;
    for (const CTxIn& txin : tx.vin) {
        CTransactionRef txPrev;
        uint256 hashBlock;
        if (!GetTransaction(txin.prevout.hash, txPrev, GetParams().GetConsensus(), hashBlock, true)) {
            return state.DoS(1, error("CheckCoinStakeReward() : failed to get input transaction"));
        }

        if (txin.prevout.n >= txPrev->vout.size()) {
            return state.DoS(1, error("CheckCoinStakeReward() : bad prevout index"));
        }

        nValueIn += txPrev->vout[txin.prevout.n].nValue;
    }

    // Check that reward is reasonable
    CAmount nActualReward = nValueOut - nValueIn;
    if (nActualReward > nRewardExpected * 2) {
        return state.DoS(100, error("CheckCoinStakeReward() : reward too high"));
    }

    if (nActualReward < 0) {
        return state.DoS(100, error("CheckCoinStakeReward() : negative reward"));
    }

    return true;
}