// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_BLOCKASSEMBLER_H
#define CLORE_BLOCKASSEMBLER_H

#include "consensus/params.h"
#include "consensus/validation.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "validation.h"

#include <memory>
#include <vector>

class CBlockIndex;
class CChainParams;
class CReserveKey;
class CScript;
class CWallet;

/** A block template is a block that is ready for mining */
class CBlockTemplate
{
public:
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOpsCost;
    std::vector<unsigned char> vchCoinbaseCommitment;
};

/** A convenience class for constructing blocks */
class BlockAssembler
{
public:
    BlockAssembler(const CChainParams& chainparams);
    /** Construct a new block template with coinbase to scriptPubKeyIn */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx = true);

private:
    // Methods
    void addPackageTxs(int& nPackagesSelected, int& nDescendantsUpdated);
    void addToBlock(CTxMemPool::txiter iter);
    void UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded, indexed_modified_transaction_set& mapModifiedTx);
    void SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set& mapModifiedTx, CTxMemPool::setEntries& failedTx);
    void SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries);
    bool TestForBlock(CTxMemPool::txiter iter);
    void resetBlock();

    // Variables
    const CChainParams& chainparams;
    CBlock* pblock;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    std::set<uint256> inBlock;
    unsigned int nBlockTx;
    unsigned int nBlockWeight;
    unsigned int nBlockSigOpsCost;
    CAmount nFees;
    CTxMemPool::setEntries inBlockTxs;
    bool fIncludeWitness;
};

#endif // CLORE_BLOCKASSEMBLER_H