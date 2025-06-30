#ifndef CLORE_CHAINSTATE_H
#define CLORE_CHAINSTATE_H

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <validation.h>
#include <txmempool.h>

// Forward declarations
struct ConnectTrace;
struct DisconnectedBlockTransactions;

class CChainState
{
public:
    CChainState() = default;
    virtual ~CChainState() = default;

    // Chain state management
    bool ActivateBestChain(CValidationState& state, const CChainParams& chainparams, std::shared_ptr<const CBlock> pblock = nullptr);
    bool ConnectTip(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexNew, const std::shared_ptr<const CBlock>& pblock, ConnectTrace& connectTrace, DisconnectedBlockTransactions& disconnectpool);
    bool DisconnectTip(CValidationState& state, const CChainParams& chainparams, DisconnectedBlockTransactions* pool = nullptr);
    bool PreciousBlock(CValidationState& state, const CChainParams& params, CBlockIndex* pindex);
    bool InvalidateBlock(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindex);
    bool ResetBlockFailureFlags(CBlockIndex* pindex);
    bool ReplayBlocks(const CChainParams& params);
    bool LoadGenesisBlock(const CChainParams& chainparams);
    void PruneBlockIndexCandidates();
    void UnloadBlockIndex();
    bool LoadBlockIndex(const CChainParams& chainparams);
    bool ActivateBestChainStep(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexMostWork, const std::shared_ptr<const CBlock>& pblock, bool& fInvalidFound, ConnectTrace& connectTrace);
    bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, const CChainParams& chainparams, bool fJustCheck = false);
    bool DisconnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, bool* pfClean = nullptr);
    bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex = nullptr);
    bool AcceptBlock(const std::shared_ptr<const CBlock>& pblock, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex, bool fRequested, const CDiskBlockPos* dbp, bool* fNewBlock);
    bool ProcessNewBlock(const CChainParams& chainparams, const std::shared_ptr<const CBlock>& pblock, bool fForceProcessing, bool* fNewBlock);
    bool ProcessNewBlockHeaders(const std::vector<CBlockHeader>& headers, CValidationState& state, const CChainParams& chainparams, const CBlockIndex** ppindex = nullptr);
    bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos* dbp = nullptr);
    void CheckBlockIndex(const Consensus::Params& consensusParams);
    bool LoadBlockIndexDB(const CChainParams& chainparams);
    bool RewindBlockIndex(const CChainParams& params);
};

#endif // CLORE_CHAINSTATE_H 