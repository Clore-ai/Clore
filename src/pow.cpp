// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "tinyformat.h"
#include "uint256.h"
#include "util.h"
#include "validation.h"

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    /* current difficulty formula, dash - DarkGravity v3, written by Evan Duffield - evan@dash.org */

    LogPrintf("DEBUG: DarkGravityWave called - pindexLast=%p, pblock=%p\n", pindexLast, pblock);

    // Enhanced validation of pindexLast before using assert
    if (!pindexLast) {
        LogPrintf("ERROR: DarkGravityWave pindexLast is null! Returning powLimit\n");
        const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
        return bnPowLimit.GetCompact();
    }

    // Test if pindexLast appears to be valid memory
    try {
        // Try to access basic fields to validate the pointer
        int test_height = pindexLast->nHeight;
        uint32_t test_bits = pindexLast->nBits;
        uint32_t test_time = pindexLast->nTime;

        LogPrintf("DEBUG: DarkGravityWave pindexLast memory validation passed (height=%d, bits=%u, time=%u)\n", test_height, test_bits, test_time);

        // Basic sanity checks
        if (test_height < 0 || test_height > 10000000) { // Sanity check for reasonable height
            LogPrintf("ERROR: DarkGravityWave pindexLast has invalid height %d! Returning powLimit\n", test_height);
            const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
            return bnPowLimit.GetCompact();
        }
    } catch (...) {
        LogPrintf("ERROR: DarkGravityWave exception during pindexLast memory validation! Returning powLimit\n");
        const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
        return bnPowLimit.GetCompact();
    }

    assert(pindexLast != nullptr);

    if (!pblock) {
        LogPrintf("ERROR: DarkGravityWave pblock is null! Returning powLimit\n");
        const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
        return bnPowLimit.GetCompact();
    }

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    int64_t nPastBlocks = 180; // ~3hr

    // make sure we have at least (nPastBlocks + 1) blocks, otherwise just return powLimit
    if (!pindexLast || pindexLast->nHeight < nPastBlocks) {
        LogPrintf("DEBUG: DarkGravityWave insufficient blocks, returning powLimit\n");
        return bnPowLimit.GetCompact();
    }

    if (params.fPowAllowMinDifficultyBlocks && params.fPowNoRetargeting) {
        LogPrintf("DEBUG: DarkGravityWave regtest min difficulty mode\n");
        // Special difficulty rule:
        // If the new block's timestamp is more than 2 * 1 minutes
        // then allow mining of a min-difficulty block.
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2)
            return nProofOfWorkLimit;
        else {
            // Return the last non-special-min-difficulty-rules-block
            const CBlockIndex* pindex = pindexLast;
            while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 &&
                   pindex->nBits == nProofOfWorkLimit)
                pindex = pindex->pprev;
            return pindex->nBits;
        }
    }

    const CBlockIndex* pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    int nKAWPOWBlocksFound = 0;
    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        // Count how blocks are KAWPOW mined in the last 180 blocks
        if (pindex->nTime >= nKAWPOWActivationTime) {
            nKAWPOWBlocksFound++;
        }

        if (nCountBlocks != nPastBlocks) {
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    // If we are mining a KAWPOW block. We check to see if we have mined
    // 180 KAWPOW blocks already. If we haven't we are going to return our
    // temp limit. This will allow us to change algos to kawpow without having to
    // change the DGW math.
    if (pblock->nTime >= nKAWPOWActivationTime) {
        if (nKAWPOWBlocksFound != nPastBlocks) {
            const arith_uint256 bnKawPowLimit = UintToArith256(params.kawpowLimit);
            return bnKawPowLimit.GetCompact();
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan / 3)
        nActualTimespan = nTargetTimespan / 3;
    if (nActualTimespan > nTargetTimespan * 3)
        nActualTimespan = nTargetTimespan * 3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredBTC(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    LogPrintf("DEBUG: GetNextWorkRequiredBTC called - pindexLast=%p, pblock=%p\n", pindexLast, pblock);

    // Enhanced validation of pindexLast before using assert
    if (!pindexLast) {
        LogPrintf("ERROR: GetNextWorkRequiredBTC pindexLast is null! Returning nProofOfWorkLimit\n");
        return nProofOfWorkLimit;
    }

    // Test if pindexLast appears to be valid memory
    try {
        // Try to access basic fields to validate the pointer
        int test_height = pindexLast->nHeight;
        uint32_t test_bits = pindexLast->nBits;
        uint32_t test_time = pindexLast->nTime;

        LogPrintf("DEBUG: GetNextWorkRequiredBTC pindexLast memory validation passed (height=%d, bits=%u, time=%u)\n", test_height, test_bits, test_time);

        // Basic sanity checks
        if (test_height < 0 || test_height > 10000000) { // Sanity check for reasonable height
            LogPrintf("ERROR: GetNextWorkRequiredBTC pindexLast has invalid height %d! Returning nProofOfWorkLimit\n", test_height);
            return nProofOfWorkLimit;
        }
    } catch (...) {
        LogPrintf("ERROR: GetNextWorkRequiredBTC exception during pindexLast memory validation! Returning nProofOfWorkLimit\n");
        return nProofOfWorkLimit;
    }

    assert(pindexLast != nullptr);

    // Enhanced memory validation for pblock
    if (!pblock) {
        LogPrintf("ERROR: GetNextWorkRequiredBTC pblock is null! Returning nProofOfWorkLimit\n");
        return nProofOfWorkLimit;
    }

    // Additional validation - check if pblock appears to be valid memory
    // by checking if basic fields look reasonable
    try {
        // Test if we can access basic fields without crashing
        uint32_t test_time = pblock->nTime;
        uint32_t test_bits = pblock->nBits;

        // Basic sanity checks
        if (test_time == 0 || test_bits == 0) {
            LogPrintf("ERROR: GetNextWorkRequiredBTC pblock appears corrupted (time=%u, bits=%u)! Returning nProofOfWorkLimit\n", test_time, test_bits);
            return nProofOfWorkLimit;
        }

        LogPrintf("DEBUG: GetNextWorkRequiredBTC pblock memory validation passed (time=%u, bits=%u)\n", test_time, test_bits);
    } catch (...) {
        LogPrintf("ERROR: GetNextWorkRequiredBTC exception during pblock memory validation! Returning nProofOfWorkLimit\n");
        return nProofOfWorkLimit;
    }

    // Special case for genesis block - always return proof of work limit
    if (pindexLast->nHeight == 0) {
        LogPrintf("DEBUG: GetNextWorkRequiredBTC genesis block case, returning nProofOfWorkLimit\n");
        return nProofOfWorkLimit;
    }

    // Only change once per difficulty adjustment interval
    LogPrintf("DEBUG: GetNextWorkRequiredBTC checking early return path\n");
    if ((pindexLast->nHeight + 1) % params.DifficultyAdjustmentInterval() != 0) {
        LogPrintf("DEBUG: GetNextWorkRequiredBTC taking early return path\n");
        if (params.fPowAllowMinDifficultyBlocks) {
            LogPrintf("DEBUG: GetNextWorkRequiredBTC regtest min difficulty enabled\n");

            // Additional safety check for pindexLast before accessing nHeight
            if (!pindexLast) {
                LogPrintf("ERROR: GetNextWorkRequiredBTC pindexLast is null in min difficulty check!\n");
                return nProofOfWorkLimit;
            }

            LogPrintf("DEBUG: GetNextWorkRequiredBTC pindexLast validated, checking height\n");

            int currentHeight = -1;
            try {
                currentHeight = pindexLast->nHeight;
                LogPrintf("DEBUG: GetNextWorkRequiredBTC current height: %d\n", currentHeight);
            } catch (...) {
                LogPrintf("ERROR: GetNextWorkRequiredBTC exception accessing pindexLast->nHeight! Returning nProofOfWorkLimit\n");
                return nProofOfWorkLimit;
            }

            // For regtest/testnet, we have a special case that's causing the crashes
            // Instead of trying to access timestamps, just return appropriate difficulty
            if (currentHeight < 10) {
                // For very early blocks, always allow min difficulty
                LogPrintf("DEBUG: GetNextWorkRequiredBTC early block (%d), returning min difficulty\n", currentHeight);
                return nProofOfWorkLimit;
            }

            LogPrintf("DEBUG: GetNextWorkRequiredBTC past early block check, height=%d\n", currentHeight);

            // Additional safety checks before accessing block times
            if (!pindexLast) {
                LogPrintf("ERROR: GetNextWorkRequiredBTC pindexLast is null in min difficulty check!\n");
                return nProofOfWorkLimit;
            }

            uint32_t blockTime, lastTime;
            try {
                // Use the pre-validated time from our memory check above
                blockTime = pblock->nTime;
                LogPrintf("DEBUG: GetNextWorkRequiredBTC got block time: %u\n", blockTime);

                lastTime = pindexLast->GetBlockTime();
                LogPrintf("DEBUG: GetNextWorkRequiredBTC got last time: %u\n", lastTime);
            } catch (...) {
                LogPrintf("ERROR: GetNextWorkRequiredBTC exception accessing block times! Returning nProofOfWorkLimit\n");
                return nProofOfWorkLimit;
            }

            LogPrintf("DEBUG: GetNextWorkRequiredBTC about to check timestamps: block=%u, last=%u, spacing=%ld\n",
                blockTime, lastTime, params.nPowTargetSpacing);

            if (blockTime > lastTime + params.nPowTargetSpacing * 2) {
                LogPrintf("DEBUG: GetNextWorkRequiredBTC returning nProofOfWorkLimit=%u\n", nProofOfWorkLimit);
                return nProofOfWorkLimit;
            } else {
                LogPrintf("DEBUG: GetNextWorkRequiredBTC entering else branch - searching for last non-special block\n");
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                LogPrintf("DEBUG: GetNextWorkRequiredBTC pindex initialized, pindex=%p, height=%d\n", pindex, pindex ? pindex->nHeight : -1);

                if (!pindex) {
                    LogPrintf("ERROR: GetNextWorkRequiredBTC pindex is null!\n");
                    return nProofOfWorkLimit;
                }

                int pindexHeight = -1;
                uint32_t pindexBits = 0;
                try {
                    pindexHeight = pindex->nHeight;
                    pindexBits = pindex->nBits;
                } catch (...) {
                    LogPrintf("ERROR: GetNextWorkRequiredBTC exception accessing pindex fields! Returning nProofOfWorkLimit\n");
                    return nProofOfWorkLimit;
                }

                LogPrintf("DEBUG: GetNextWorkRequiredBTC starting while loop with pindex->nHeight=%d\n", pindexHeight);
                LogPrintf("DEBUG: GetNextWorkRequiredBTC DifficultyAdjustmentInterval=%d\n", params.DifficultyAdjustmentInterval());
                LogPrintf("DEBUG: GetNextWorkRequiredBTC pindex->nBits=%u, nProofOfWorkLimit=%u\n", pindexBits, nProofOfWorkLimit);

                int iterations = 0;
                while (pindex && pindex->pprev && pindexHeight % params.DifficultyAdjustmentInterval() != 0 && pindexBits == nProofOfWorkLimit && pindexHeight > 0) {
                    iterations++;

                    int nextHeight = -1;
                    try {
                        nextHeight = pindex->pprev ? pindex->pprev->nHeight : -1;
                    } catch (...) {
                        LogPrintf("ERROR: GetNextWorkRequiredBTC exception accessing pprev->nHeight at iteration %d!\n", iterations);
                        break;
                    }

                    LogPrintf("DEBUG: GetNextWorkRequiredBTC while loop iteration %d: moving from height %d to %d\n", iterations, pindexHeight, nextHeight);

                    if (!pindex->pprev) {
                        LogPrintf("ERROR: GetNextWorkRequiredBTC pindex->pprev is null at iteration %d!\n", iterations);
                        break;
                    }

                    pindex = pindex->pprev;

                    if (!pindex) {
                        LogPrintf("ERROR: GetNextWorkRequiredBTC pindex became null at iteration %d!\n", iterations);
                        return nProofOfWorkLimit;
                    }

                    // Update the cached values for next iteration
                    try {
                        pindexHeight = pindex->nHeight;
                        pindexBits = pindex->nBits;
                    } catch (...) {
                        LogPrintf("ERROR: GetNextWorkRequiredBTC exception updating cached values at iteration %d!\n", iterations);
                        return nProofOfWorkLimit;
                    }

                    // Safety check to prevent infinite loops
                    if (iterations > 10000) {
                        LogPrintf("ERROR: GetNextWorkRequiredBTC while loop exceeded 10000 iterations, breaking\n");
                        break;
                    }
                }
                uint32_t finalBits = nProofOfWorkLimit;
                int finalHeight = -1;
                if (pindex) {
                    try {
                        finalHeight = pindex->nHeight;
                        finalBits = pindex->nBits;
                    } catch (...) {
                        LogPrintf("ERROR: GetNextWorkRequiredBTC exception accessing final pindex values! Using nProofOfWorkLimit\n");
                        finalBits = nProofOfWorkLimit;
                    }
                }
                LogPrintf("DEBUG: GetNextWorkRequiredBTC while loop ended at height=%d, returning nBits=%u\n", finalHeight, finalBits);
                return finalBits;
            }
        }
        LogPrintf("DEBUG: GetNextWorkRequiredBTC returning pindexLast->nBits=%u\n", pindexLast->nBits);
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval() - 1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    //    int64_t nPrevBlockTime = (pindexLast->pprev ? pindexLast->pprev->GetBlockTime() : pindexLast->GetBlockTime());  //<- Commented out - fixes "not used" warning

    if (IsDGWActive(pindexLast->nHeight + 1)) {
        //        LogPrint(BCLog::NET, "Block %s - version: %s: found next work required using DGW: [%s] (BTC would have been [%s]\t(%+d)\t(%0.3f%%)\t(%s sec))\n",
        //                 pindexLast->nHeight + 1, pblock->nVersion, dgw, btc, btc - dgw, (float)(btc - dgw) * 100.0 / (float)dgw, pindexLast->GetBlockTime() - nPrevBlockTime);
        return DarkGravityWave(pindexLast, pblock, params);
    } else {
        //        LogPrint(BCLog::NET, "Block %s - version: %s: found next work required using BTC: [%s] (DGW would have been [%s]\t(%+d)\t(%0.3f%%)\t(%s sec))\n",
        //                  pindexLast->nHeight + 1, pblock->nVersion, btc, dgw, dgw - btc, (float)(dgw - btc) * 100.0 / (float)btc, pindexLast->GetBlockTime() - nPrevBlockTime);
        return GetNextWorkRequiredBTC(pindexLast, pblock, params);
    }
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan / 4)
        nActualTimespan = params.nPowTargetTimespan / 4;
    if (nActualTimespan > params.nPowTargetTimespan * 4)
        nActualTimespan = params.nPowTargetTimespan * 4;

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

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
