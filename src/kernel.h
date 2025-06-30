// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2021 The PIVX developers
// Copyright (c) 2024 The CLORE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_KERNEL_H
#define CLORE_KERNEL_H

#include "amount.h"
#include "chain.h"
#include "streams.h"
#include "uint256.h"

// Forward declarations
#include "stakeinput.h"

class CBlock;
class CBlockIndex;
class COutPoint;
class CTransaction;
class CWallet;

// MODIFIER_INTERVAL: time to elapse before new modifier is computed
static const unsigned int MODIFIER_INTERVAL = 6 * 60 * 60;
static const int MODIFIER_INTERVAL_RATIO = 3;

// Compute the hash modifier for proof-of-stake
bool ComputeNextStakeModifier(const CBlockIndex* pindexCurrent, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier);

// Check whether the hash satisfies the proof-of-stake requirement
bool CheckStakeKernelHash(unsigned int nBits, const CBlockIndex& blockFrom, const CTransaction& txPrev, const COutPoint& prevout, unsigned int nTimeTx, uint256& hashProofOfStake, bool fVerify = true);

// Initialize stake modifier
void InitializeStakeModifier(const CBlockIndex* pindexGenesisBlock, uint64_t& nStakeModifier);

// Get stake modifier checksum
uint32_t GetStakeModifierChecksum(const CBlockIndex* pindex);

// Check stake modifier checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, uint32_t nStakeModifierChecksum);

// Wrapper around the stake hash check
bool CheckProofOfStake(const CBlockIndex* pindexCheck, const CTransaction& tx, const uint256& hashProofOfStake, std::unique_ptr<CStakeInput>& stake);

// Stake Modifier V2 (Time Protocol v2)
uint256 ComputeStakeModifierV2(const CBlockIndex* pindexPrev, const uint256& kernel);
bool CheckStakeKernelHashV2(const CBlockIndex* pindexPrev, const CTransaction& txPrev, const COutPoint& prevout, unsigned int nTimeTx, uint256& hashProofOfStake, bool fVerify = true);

#endif // CLORE_KERNEL_H