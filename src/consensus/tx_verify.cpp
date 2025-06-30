// Copyright (c) 2017-2017 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tx_verify.h"
#include "chainparams.h"
#include <assets/assets.h>
#include <script/standard.h>
#include <util.h>
#include <validation.h>

#include "consensus.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "validation.h"
#include <base58.h>
#include <cmath>
#include <tinyformat.h>
#include <wallet/wallet.h>

// TODO remove the following dependencies
#include "chain.h"
#include "coins.h"
#include "utilmoneystr.h"

bool IsFinalTx(const CTransaction& tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction& tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2 && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight - 1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction& tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const auto& txin : tx.vin) {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const auto& txout : tx.vout) {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut& prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    int64_t nSigOps = GetLegacySigOpCount(tx) * WITNESS_SCALE_FACTOR;

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs) * WITNESS_SCALE_FACTOR;
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut& prevout = coin.out;
        nSigOps += CountWitnessSigOps(tx.vin[i].scriptSig, prevout.scriptPubKey, &tx.vin[i].scriptWitness, flags);
    }
    return nSigOps;
}

bool CheckTransaction(const CTransaction& tx, CValidationState& state, bool fCheckDuplicateInputs, bool fMempoolCheck, bool fAssets)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout) {
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs - only if explicitly requested for performance
    if (fCheckDuplicateInputs) {
        std::set<COutPoint> vInOutPoints;
        for (const auto& txin : tx.vin) {
            if (!vInOutPoints.insert(txin.prevout).second)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        }
    }

    if (tx.IsCoinBase()) {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    } else {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
            strprintf("%s: inputs missing/spent", __func__), tx.GetHash());
    }

    CAmount nValueIn = 0;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint& prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < COINBASE_MATURITY) {
            return state.Invalid(false,
                REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        // Check for negative or overflow input values
        nValueIn += coin.out.nValue;
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange", false, "", tx.GetHash());
        }
    }

    const CAmount value_out = tx.GetValueOut(AreEnforcedValuesDeployed());
    if (nValueIn < value_out) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
            strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(value_out)), tx.GetHash());
    }

    // Tally transaction fees
    const CAmount txfee_aux = nValueIn - value_out;
    if (!MoneyRange(txfee_aux)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-out-of-range", false, "", tx.GetHash());
    }

    txfee = txfee_aux;
    return true;
}

//! Check to make sure that the inputs and outputs CAmount match exactly.
bool Consensus::CheckTxAssets(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, CAssetsCache* assetCache, bool fCheckMempool, std::vector<std::pair<std::string, uint256>>& vPairReissueAssets, const bool fRunningUnitTests, std::set<CMessage>* setMessages, int64_t nBlocktime, std::vector<std::pair<std::string, CNullAssetTxData>>* myNullAssetData)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missing-or-spent", false,
            strprintf("%s: inputs missing/spent", __func__), tx.GetHash());
    }

    // Create map that stores the amount of an asset transaction input. Used to verify no assets are burned
    std::map<std::string, CAmount> totalInputs;

    std::map<std::string, std::string> mapAddresses;

    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint& prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        if (coin.IsAsset()) {
            CAssetOutputEntry data;
            if (!GetAssetData(coin.out.scriptPubKey, data))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-failed-to-get-asset-from-script", false, "", tx.GetHash());

            // Add to the total value of assets in the inputs
            if (totalInputs.count(data.assetName))
                totalInputs.at(data.assetName) += data.nAmount;
            else
                totalInputs.insert(make_pair(data.assetName, data.nAmount));

            if (AreMessagesDeployed()) {
                mapAddresses.insert(make_pair(data.assetName, EncodeDestination(data.destination)));
            }

            if (IsAssetNameAnRestricted(data.assetName)) {
                if (assetCache->CheckForAddressRestriction(data.assetName, EncodeDestination(data.destination), true)) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-restricted-asset-transfer-from-frozen-address", false, "", tx.GetHash());
                }
            }
        }
    }

    // Create map that stores the amount of an asset transaction output. Used to verify no assets are burned
    std::map<std::string, CAmount> totalOutputs;
    int index = 0;
    int64_t currentTime = GetTime();
    std::string strError = "";
    int i = 0;
    for (const auto& txout : tx.vout) {
        i++;
        bool fIsAsset = false;
        int nType = 0;
        bool fIsOwner = false;
        if (txout.scriptPubKey.IsAssetScript(nType, fIsOwner))
            fIsAsset = true;

        if (assetCache) {
            if (fIsAsset && !AreAssetsDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-is-asset-and-asset-not-active");

            if (txout.scriptPubKey.IsNullAsset()) {
                if (!AreRestrictedAssetsDeployed())
                    return state.DoS(100, false, REJECT_INVALID,
                        "bad-tx-null-asset-data-before-restricted-assets-activated");

                if (txout.scriptPubKey.IsNullAssetTxDataScript()) {
                    if (!ContextualCheckNullAssetTxOut(txout, assetCache, strError, myNullAssetData))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else if (txout.scriptPubKey.IsNullGlobalRestrictionAssetTxDataScript()) {
                    if (!ContextualCheckGlobalAssetTxOut(txout, assetCache, strError))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else if (txout.scriptPubKey.IsNullAssetVerifierTxDataScript()) {
                    if (!ContextualCheckVerifierAssetTxOut(txout, assetCache, strError))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else {
                    return state.DoS(100, false, REJECT_INVALID, "bad-tx-null-asset-data-unknown-type", false, "", tx.GetHash());
                }
            }
        }

        if (nType == TX_TRANSFER_ASSET) {
            CAssetTransfer transfer;
            std::string address = "";
            if (!TransferAssetFromScript(txout.scriptPubKey, transfer, address))
                return state.DoS(100, false, REJECT_INVALID, "bad-tx-asset-transfer-bad-deserialize", false, "", tx.GetHash());

            if (!ContextualCheckTransferAsset(assetCache, transfer, address, strError))
                return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());

            // Add to the total value of assets in the outputs
            if (totalOutputs.count(transfer.strName))
                totalOutputs.at(transfer.strName) += transfer.nAmount;
            else
                totalOutputs.insert(make_pair(transfer.strName, transfer.nAmount));

            if (!fRunningUnitTests) {
                if (IsAssetNameAnOwner(transfer.strName)) {
                    if (transfer.nAmount != OWNER_ASSET_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-owner-amount-was-not-1", false, "", tx.GetHash());
                } else {
                    // For all other types of assets, make sure they are sending the right type of units
                    CNewAsset asset;
                    if (!assetCache->GetAssetMetaDataIfExists(transfer.strName, asset))
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-asset-not-exist", false, "", tx.GetHash());

                    if (asset.strName != transfer.strName)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-asset-database-corrupted", false, "", tx.GetHash());

                    if (!CheckAmountWithUnits(transfer.nAmount, asset.units))
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-asset-amount-not-match-units", false, "", tx.GetHash());
                }
            }

            /** Get messages from the transaction, only used when getting called from ConnectBlock **/
            // Get the messages from the Tx unless they are expired
            if (AreMessagesDeployed() && fMessaging && setMessages) {
                if (IsAssetNameAnOwner(transfer.strName) || IsAssetNameAnMsgChannel(transfer.strName)) {
                    if (!transfer.message.empty()) {
                        if (transfer.nExpireTime == 0 || transfer.nExpireTime > currentTime) {
                            if (mapAddresses.count(transfer.strName)) {
                                if (mapAddresses.at(transfer.strName) == address) {
                                    COutPoint out(tx.GetHash(), index);
                                    CMessage message(out, transfer.strName, transfer.message,
                                        transfer.nExpireTime, nBlocktime);
                                    setMessages->insert(message);
                                    LogPrintf("Got message: %s\n", message.ToString()); // TODO remove after testing
                                }
                            }
                        }
                    }
                }
            }
        } else if (nType == TX_REISSUE_ASSET) {
            CReissueAsset reissue;
            std::string address;
            if (!ReissueAssetFromScript(txout.scriptPubKey, reissue, address))
                return state.DoS(100, false, REJECT_INVALID, "bad-tx-asset-reissue-bad-deserialize", false, "", tx.GetHash());

            if (mapReissuedAssets.count(reissue.strName)) {
                if (mapReissuedAssets.at(reissue.strName) != tx.GetHash())
                    return state.DoS(100, false, REJECT_INVALID, "bad-tx-reissue-chaining-not-allowed", false, "", tx.GetHash());
            } else {
                vPairReissueAssets.emplace_back(std::make_pair(reissue.strName, tx.GetHash()));
            }
        }
        index++;
    }

    if (assetCache) {
        if (tx.IsNewAsset()) {
            // Get the asset type
            CNewAsset asset;
            std::string address;
            if (!AssetFromScript(tx.vout[tx.vout.size() - 1].scriptPubKey, asset, address)) {
                error("%s : Failed to get new asset from transaction: %s", __func__, tx.GetHash().GetHex());
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-serialzation-failed", false, "", tx.GetHash());
            }

            AssetType assetType;
            IsAssetNameValid(asset.strName, assetType);

            if (!ContextualCheckNewAsset(assetCache, asset, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, strError);

        } else if (tx.IsReissueAsset()) {
            CReissueAsset reissue_asset;
            std::string address;
            if (!ReissueAssetFromScript(tx.vout[tx.vout.size() - 1].scriptPubKey, reissue_asset, address)) {
                error("%s : Failed to get new asset from transaction: %s", __func__, tx.GetHash().GetHex());
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-reissue-serialzation-failed", false, "", tx.GetHash());
            }
            if (!ContextualCheckReissueAsset(assetCache, reissue_asset, strError, tx))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-reissue-contextual-" + strError, false, "", tx.GetHash());
        } else if (tx.IsNewUniqueAsset()) {
            if (!ContextualCheckUniqueAssetTx(assetCache, strError, tx))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-unique-contextual-" + strError, false, "", tx.GetHash());
        } else if (tx.IsNewMsgChannelAsset()) {
            if (!AreMessagesDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-msgchannel-before-messaging-is-active", false, "", tx.GetHash());

            CNewAsset asset;
            std::string strAddress;
            if (!MsgChannelAssetFromTransaction(tx, asset, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-msgchannel-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewAsset(assetCache, asset, strError, fCheckMempool))
                return state.DoS(100, error("%s: %s", __func__, strError), REJECT_INVALID,
                    "bad-txns-issue-msgchannel-contextual-" + strError);
        } else if (tx.IsNewQualifierAsset()) {
            if (!AreRestrictedAssetsDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualifier-before-it-is-active", false, "", tx.GetHash());

            CNewAsset asset;
            std::string strAddress;
            if (!QualifierAssetFromTransaction(tx, asset, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualifier-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewAsset(assetCache, asset, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualfier-contextual" + strError, false, "", tx.GetHash());

        } else if (tx.IsNewRestrictedAsset()) {
            if (!AreRestrictedAssetsDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-before-it-is-active", false, "", tx.GetHash());

            // Get asset data
            CNewAsset asset;
            std::string strAddress;
            if (!RestrictedAssetFromTransaction(tx, asset, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewAsset(assetCache, asset, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-contextual" + strError, false, "", tx.GetHash());

            // Get verifier string
            CNullAssetTxVerifierString verifier;
            if (!tx.GetVerifierStringFromTx(verifier, strError))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-verifier-search-" + strError, false, "", tx.GetHash());

            // Check the verifier string against the destination address
            if (!ContextualCheckVerifierString(assetCache, verifier.verifier_string, strAddress, strError))
                return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());

        } else {
            for (auto out : tx.vout) {
                int nType;
                bool _isOwner;
                if (out.scriptPubKey.IsAssetScript(nType, _isOwner)) {
                    if (nType != TX_TRANSFER_ASSET) {
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-bad-asset-transaction", false, "", tx.GetHash());
                    }
                } else {
                    if (out.scriptPubKey.Find(OP_CLORE_ASSET)) {
                        if (AreRestrictedAssetsDeployed()) {
                            if (out.scriptPubKey[0] != OP_CLORE_ASSET) {
                                return state.DoS(100, false, REJECT_INVALID,
                                    "bad-txns-op-clore-asset-not-in-right-script-location", false, "", tx.GetHash());
                            }
                        } else {
                            return state.DoS(100, false, REJECT_INVALID, "bad-txns-bad-asset-script", false, "", tx.GetHash());
                        }
                    }
                }
            }
        }
    }

    for (const auto& outValue : totalOutputs) {
        if (!totalInputs.count(outValue.first)) {
            std::string errorMsg;
            errorMsg = strprintf("Bad Transaction - Trying to create outpoint for asset that you don't have: %s", outValue.first);
            return state.DoS(100, false, REJECT_INVALID, "bad-tx-inputs-outputs-mismatch " + errorMsg, false, "", tx.GetHash());
        }

        if (totalInputs.at(outValue.first) != outValue.second) {
            std::string errorMsg;
            errorMsg = strprintf("Bad Transaction - Assets would be burnt %s", outValue.first);
            return state.DoS(100, false, REJECT_INVALID, "bad-tx-inputs-outputs-mismatch " + errorMsg, false, "", tx.GetHash());
        }
    }

    // Check the input size and the output size
    if (totalOutputs.size() != totalInputs.size()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-asset-inputs-size-does-not-match-outputs-size", false, "", tx.GetHash());
    }
    return true;
}
