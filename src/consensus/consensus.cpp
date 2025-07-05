// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2022 The CLORE.AI
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus.h"
#include <validation.h>
#include "params.h"
#include "util.h"
#include "timedata.h"

unsigned int GetMaxBlockWeight()
{
    // Now that Assets have gone live, we should make checks against the new larger block size only
    // This is necessary because when the chain loads, it can fail certain blocks(that are valid) when
    // The asset active state isn't set like during a reindex
    return MAX_BLOCK_WEIGHT_HIP2;

    // Old block weight for when assets weren't activated
//    return MAX_BLOCK_WEIGHT;
}

unsigned int GetMaxBlockSerializedSize()
{
    // Now that Assets have gone live, we should make checks against the new larger block size only
    // This is necessary because when the chain loads, it can fail certain blocks(that are valid) when
    // The asset active state isn't set like during a reindex
    return MAX_BLOCK_SERIALIZED_SIZE_HIP2;

    // Old block serialized size for when assets weren't activated
//    return MAX_BLOCK_SERIALIZED_SIZE;
}

namespace Consensus {

bool Params::HasStakeMinAgeOrDepth(const int contextHeight, const uint32_t contextTime, const int utxoFromBlockHeight, const uint32_t utxoFromBlockTime) const
{
    // before stake modifier V2, we require the utxo to be nStakeMinAge old
    if (!NetworkUpgradeActive(contextHeight, Consensus::ENABLE_POS_VALIDATORS))
        return (utxoFromBlockTime + nStakeMinAge <= contextTime);
    // with stake modifier V2+, we require the utxo to be nStakeMinDepth deep in the chain
    return (contextHeight - utxoFromBlockHeight >= nStakeMinDepth);
}

int64_t Params::FutureBlockTimeDrift(const int nHeight) const
{
    // Return appropriate time drift based on PoS activation status
    if (NetworkUpgradeActive(nHeight, Consensus::ENABLE_POS_STAKING)) {
        return nFutureTimeDriftPoS;
    }
    return nFutureTimeDriftPoW;
}

bool Params::IsValidBlockTimeStamp(const int64_t nTime, const int nHeight) const
{
    // Validate timestamp is not too far in the future
    int64_t maxFutureTime = GetAdjustedTime() + FutureBlockTimeDrift(nHeight);
    return nTime <= maxFutureTime;
}

} // namespace Consensus
