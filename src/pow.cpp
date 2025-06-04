// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"
#include "validation.h"
#include "chainparams.h"
#include "tinyformat.h"

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params) {
    /* current difficulty formula, dash - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    assert(pindexLast != nullptr);

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    int64_t nPastBlocks = 180; // ~3hr

    // make sure we have at least (nPastBlocks + 1) blocks, otherwise just return powLimit
    if (!pindexLast || pindexLast->nHeight < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    if (params.fPowAllowMinDifficultyBlocks && params.fPowNoRetargeting) {
        // Special difficulty rule:
        // If the new block's timestamp is more than 2 * 1 minutes
        // then allow mining of a min-difficulty block.
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2)
            return nProofOfWorkLimit;
        else {
            // Return the last non-special-min-difficulty-rules-block
            const CBlockIndex *pindex = pindexLast;
            while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 &&
                   pindex->nBits == nProofOfWorkLimit)
                pindex = pindex->pprev;
            return pindex->nBits;
        }
    }

    const CBlockIndex *pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    int nEQUIHASHBlocksFound = 0;
    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        // Count how blocks are EQUIHASH mined in the last 180 blocks
        if (pindex->nTime >= params.equihashHeight) {
            nEQUIHASHBlocksFound++;
        }

        if(nCountBlocks != nPastBlocks) {
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    // If we are mining a EQUIHASH block. We check to see if we have mined
    // 180 EQUIHASH blocks already. If we haven't we are going to return our
    // temp limit. This will allow us to change algos to EQUIHASH without having to
    // change the DGW math.
    if (pblock->nTime >= params.equihashHeight) {
        if (nEQUIHASHBlocksFound != nPastBlocks) {
            const arith_uint256 bnEQUIHASHLimit = UintToArith256(params.equihashLimit);
            return bnEQUIHASHLimit.GetCompact();
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredPOS(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    const Consensus::Params& consensus = GetParams().GetConsensus();

    // if (consensus.fPowNoRetargeting)
    //     return pindexLast->nBits;

    // const CBlockIndex* BlockLastSolved = pindexLast;
    // const CBlockIndex* BlockReading = pindexLast;
    // int64_t nActualTimespan = 0;
    // int64_t LastBlockTime = 0;
    // int64_t PastBlocksMin = 24;
    // int64_t PastBlocksMax = 24;
    // int64_t CountBlocks = 0;
    // arith_uint256 PastDifficultyAverage;
    // arith_uint256 PastDifficultyAveragePrev;
    // const arith_uint256& powLimit = UintToArith256(consensus.powLimit);

    // if (BlockLastSolved == nullptr || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin) {
    //     return powLimit.GetCompact();
    // }

    // const arith_uint256& bnTargetLimit = UintToArith256(consensus.ProofOfStakeLimit(true));
    // const int64_t& nTargetTimespan = consensus.TargetTimespan(true);

    // int64_t nActualSpacing = 0;
    // if (pindexLast->nHeight != 0)
    //     nActualSpacing = pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime();
    // if (nActualSpacing < 0)
    //     nActualSpacing = 1;
    // if (nActualSpacing > consensus.nTargetSpacing*10)
    //     nActualSpacing = consensus.nTargetSpacing*10;

    // // ppcoin: target change every block
    // // ppcoin: retarget with exponential moving toward target spacing
    // arith_uint256 bnNew;
    // bnNew.SetCompact(pindexLast->nBits);

    // int64_t nInterval = nTargetTimespan / consensus.nTargetSpacing;
    // bnNew *= ((nInterval - 1) * consensus.nTargetSpacing + nActualSpacing + nActualSpacing);
    // bnNew /= ((nInterval + 1) * consensus.nTargetSpacing);

    // if (bnNew <= 0 || bnNew > bnTargetLimit)
    //     bnNew = bnTargetLimit;

    // return bnNew.GetCompact();
     return UintToArith256(consensus.ProofOfStakeLimit(true)).GetCompact();
}

unsigned int GetNextWorkRequiredBTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
//    int64_t nPrevBlockTime = (pindexLast->pprev ? pindexLast->pprev->GetBlockTime() : pindexLast->GetBlockTime());  //<- Commented out - fixes "not used" warning
    if (pindexLast->nHeight + 1 >= params.posHeight) {
        return GetNextWorkRequiredPOS(pindexLast, pblock, params);
    } else {
        if (IsDGWActive(pindexLast->nHeight + 1)) {
    //        LogPrint(BCLog::NET, "Block %s - version: %s: found next work required using DGW: [%s] (BTC would have been [%s]\t(%+d)\t(%0.3f%%)\t(%s sec))\n",
    //                 pindexLast->nHeight + 1, pblock->nVersion, dgw, btc, btc - dgw, (float)(btc - dgw) * 100.0 / (float)dgw, pindexLast->GetBlockTime() - nPrevBlockTime);
            return DarkGravityWave(pindexLast, pblock, params);
        }
        else {
    //        LogPrint(BCLog::NET, "Block %s - version: %s: found next work required using BTC: [%s] (DGW would have been [%s]\t(%+d)\t(%0.3f%%)\t(%s sec))\n",
    //                  pindexLast->nHeight + 1, pblock->nVersion, btc, dgw, dgw - btc, (float)(dgw - btc) * 100.0 / (float)btc, pindexLast->GetBlockTime() - nPrevBlockTime);
            return GetNextWorkRequiredBTC(pindexLast, pblock, params);
        }
    }

}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit)) {
        LogPrintf("[CheckProofOfWork] Invalid target range: %s\n", bnTarget.ToString());
        return false;
    }

    if (UintToArith256(hash) > bnTarget) {
        LogPrintf("[CheckProofOfWork] PoW failed: hash=%s > target=%s\n", hash.ToString(), bnTarget.ToString());
        return false;
    }

    LogPrintf("[CheckProofOfWork] PoW accepted: hash=%s <= target=%s\n", hash.ToString(), bnTarget.ToString());
    return true;
}

uint256 GetPOWHash(const CBlockHeader& block, int height, const Consensus::Params& params)
{
    uint256 hash;

    hash = block.GetX16RHash();

    return hash;
}
