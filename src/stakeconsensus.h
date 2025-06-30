// Copyright (c) 2017-2021 The PIVX developers
// Copyright (c) 2024 The CLORE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_STAKECONSENSUS_H
#define CLORE_STAKECONSENSUS_H

#include "amount.h"
#include "arith_uint256.h"
#include "uint256.h"
#include <memory>

class CBlock;
class CBlockIndex;
class COutPoint;
class CStakeInput;
class CTransaction;
class CValidationState;

// Stake validation functions
bool CheckStakeKernelHash(const CBlockIndex* pindexPrev,
    const CTransaction& txPrev,
    const COutPoint& prevout,
    unsigned int nTimeTx,
    unsigned int nHashDrift,
    bool fCheck,
    uint256& hashProofOfStake);

bool CheckProofOfStake(const CBlock& block,
    uint256& hashProofOfStake,
    std::unique_ptr<CStakeInput>& stake,
    CValidationState& state);

bool GetStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier);

// Stake modifier functions

bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier);

// Kernel hash functions
uint256 ComputeStakeKernelHash(const COutPoint& prevout,
    unsigned int nTimeTx,
    uint64_t nStakeModifier,
    const uint256& hashBlockFrom);

// Stake target functions
arith_uint256 GetStakeTargetLimit(bool fProofOfStake, int nHeight);
bool CheckStakeTarget(const arith_uint256& bnTarget, int nHeight, unsigned int nTime, unsigned int nTimePrev);

// Stake reward functions
CAmount GetStakeReward(const CBlockIndex* pindex, CAmount nCoinAge, unsigned int nTime);

// Time slot functions
int64_t GetCurrentTimeSlot();
int64_t GetTimeSlot(int64_t nTime);
bool IsValidTimeSlot(int64_t nTime, int64_t nPrevTime);

// Coinstake validation
bool IsValidCoinStake(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);
bool CheckCoinStakeReward(const CTransaction& tx, CAmount nRewardExpected, CValidationState& state);

#endif // CLORE_STAKECONSENSUS_H