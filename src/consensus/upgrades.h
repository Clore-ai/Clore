// Copyright (c) 2015-2020 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_CONSENSUS_UPGRADES_H
#define CLORE_CONSENSUS_UPGRADES_H

#include "consensus/params.h"
#include <boost/optional.hpp>

namespace Consensus
{


struct NetworkUpgradeInfo {
    /** User-facing name for the upgrade */
    std::string strName;
    /** User-facing information string about the upgrade */
    std::string strInfo;
};

extern const struct NetworkUpgradeInfo NetworkUpgradeInfo[];

enum UpgradeState {
    UPGRADE_DISABLED,
    UPGRADE_PENDING,
    UPGRADE_ACTIVE
};

/**
 * Checks the state of a given network upgrade based on block height.
 * Caller must check that the height is >= 0 (and handle unknown heights).
 */
UpgradeState NetworkUpgradeState(
    int nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx);

/**
 * Returns true if the given network upgrade is active as of the given block
 * height. Caller must check that the height is >= 0 (and handle unknown
 * heights).
 */
bool NetworkUpgradeActive(
    int nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx);

} // namespace Consensus

#endif // CLORE_CONSENSUS_UPGRADES_H