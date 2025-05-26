// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2022 The CLORE.AI
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "checkpoints.h"

#include "chain.h"
#include "chainparams.h"
#include "reverse_iterator.h"
#include "validation.h"
#include "uint256.h"

#include <stdint.h>


namespace Checkpoints {

    bool fEnabled = true;

    bool CheckBlock(int nHeight, const uint256& hash, bool fMatchesCheckpoint)
    {
        if (!fEnabled)
            return true;

        const MapCheckpoints& checkpoints = *Params().Checkpoints().mapCheckpoints;

        MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
        // If looking for an exact match, then return false
        if (i == checkpoints.end()) return !fMatchesCheckpoint;
        return hash == i->second;
    }

    CBlockIndex* GetLastCheckpoint(const CCheckpointData& data)
    {
        const MapCheckpoints& checkpoints = data.mapCheckpoints;

        for (const MapCheckpoints::value_type& i : reverse_iterate(checkpoints))
        {
            const uint256& hash = i.second;
            BlockMap::const_iterator t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
                return t->second;
        }
        return nullptr;
    }

} // namespace Checkpoints
