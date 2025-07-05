// Copyright (c) 2015-2020 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/upgrades.h"
#include "consensus/params.h"

namespace Consensus
{

const struct NetworkUpgradeInfo NetworkUpgradeInfo[] = {
    {
        /*.strName =*/"Base Network",
        /*.strInfo =*/"The base network rules",
    },
    {
        /*.strName =*/"Validator Activation",
        /*.strInfo =*/"Enables validator nodes and stake-based consensus preparation",
    },
    {
        /*.strName =*/"PoS Preparation",
        /*.strInfo =*/"Enables Proof of Stake preparation phase (hybrid PoW/PoS)",
    },
    {
        /*.strName =*/"PoS Completion",
        /*.strInfo =*/"Enables Proof of Stake completion phase - PoW mining permanently disabled",
    },
    {
        /*.strName =*/"Test Dummy",
        /*.strInfo =*/"Test dummy upgrade",
    },
};

UpgradeState NetworkUpgradeState(
    int nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx)
{
    if (nHeight < 0 || idx >= Consensus::MAX_NETWORK_UPGRADES)
        return UPGRADE_DISABLED;

    auto nActivationHeight = params.vUpgrades[idx].nActivationHeight;

    if (nActivationHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT) {
        return UPGRADE_DISABLED;
    } else if (nHeight >= nActivationHeight) {
        return UPGRADE_ACTIVE;
    } else {
        return UPGRADE_PENDING;
    }
}

bool NetworkUpgradeActive(
    int nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx)
{
    return NetworkUpgradeState(nHeight, params, idx) == UPGRADE_ACTIVE;
}

bool Params::NetworkUpgradeActive(int nHeight, Consensus::UpgradeIndex idx) const
{
    if (idx >= MAX_NETWORK_UPGRADES)
        return false;

    if (nHeight < 0)
        return false;

    return nHeight >= vUpgrades[idx].nActivationHeight;
}

} // namespace Consensus