// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2021-2022 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/providertx.h"

#include "bls/key_io.h"
#include "key_io.h"

std::string ProRegPL::MakeSignString() const
{
    std::ostringstream ss;

    ss << HexStr(scriptPayout) << "|";
    ss << strprintf("%d", nOperatorReward) << "|";
    ss << EncodeDestination(keyIDOwner) << "|";
    ss << EncodeDestination(keyIDVoting) << "|";

    // ... and also the full hash of the payload as a protection against malleability and replays
    ss << ::SerializeHash(*this).ToString();

    return ss.str();
}

std::string ProRegPL::ToString() const
{
    CTxDestination dest;
    std::string payee = ExtractDestination(scriptPayout, dest) ?
                        EncodeDestination(dest) : "unknown";
    return strprintf("ProRegPL(nVersion=%d, collateralOutpoint=%s, addr=%s, nOperatorReward=%f, ownerAddress=%s, operatorPubKey=%s, votingAddress=%s, scriptPayout=%s)",
        nVersion, collateralOutpoint.ToStringShort(), addr.ToString(), (double)nOperatorReward / 100, EncodeDestination(keyIDOwner), bls::EncodePublic(Params(), pubKeyOperator), EncodeDestination(keyIDVoting), payee);
}

void ProRegPL::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.pushKV("version", nVersion);
    obj.pushKV("collateralHash", collateralOutpoint.hash.ToString());
    obj.pushKV("collateralIndex", (int)collateralOutpoint.n);
    obj.pushKV("service", addr.ToString());
    obj.pushKV("ownerAddress", EncodeDestination(keyIDOwner));
    obj.pushKV("operatorPubKey", bls::EncodePublic(Params(), pubKeyOperator));
    obj.pushKV("votingAddress", EncodeDestination(keyIDVoting));

    CTxDestination dest1;
    if (ExtractDestination(scriptPayout, dest1)) {
        obj.pushKV("payoutAddress", EncodeDestination(dest1));
    }
    CTxDestination dest2;
    if (ExtractDestination(scriptOperatorPayout, dest2)) {
        obj.pushKV("operatorPayoutAddress", EncodeDestination(dest2));
    }
    obj.pushKV("operatorReward", (double)nOperatorReward / 100);
    obj.pushKV("inputsHash", inputsHash.ToString());
}

bool ProRegPL::IsTriviallyValid(CValidationState& state) const
{
    if (nVersion == 0 || nVersion > ProRegPL::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }
    if (nType != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }
    if (nMode != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-mode");
    }

    if (keyIDOwner.IsNull() || keyIDVoting.IsNull()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-null");
    }
    if (!pubKeyOperator.IsValid()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-key-invalid");
    }
    // we may support other kinds of scripts later, but restrict it for now
    if (!scriptPayout.IsPayToPublicKeyHash()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
    }
    if (!scriptOperatorPayout.empty() && !scriptOperatorPayout.IsPayToPublicKeyHash()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-payee");
    }

    CTxDestination payoutDest;
    if (!ExtractDestination(scriptPayout, payoutDest)) {
        // should not happen as we checked script types before
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-dest");
    }
    // don't allow reuse of payout key for other keys (don't allow people to put the payee key onto an online server)
    if (payoutDest == CTxDestination(keyIDOwner) ||
        payoutDest == CTxDestination(keyIDVoting)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-reuse");
    }

    if (nOperatorReward > 10000) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-reward");
    }
    return true;
}

std::string ProUpServPL::ToString() const
{
    CTxDestination dest;
    std::string payee = ExtractDestination(scriptOperatorPayout, dest) ?
                        EncodeDestination(dest) : "unknown";
    return strprintf("ProUpServPL(nVersion=%d, proTxHash=%s, addr=%s, operatorPayoutAddress=%s)",
        nVersion, proTxHash.ToString(), addr.ToString(), payee);
}

void ProUpServPL::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.pushKV("version", nVersion);
    obj.pushKV("proTxHash", proTxHash.ToString());
    obj.pushKV("service", addr.ToString());
    CTxDestination dest;
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        obj.pushKV("operatorPayoutAddress", EncodeDestination(dest));
    }
    obj.pushKV("inputsHash", inputsHash.ToString());
}

bool ProUpServPL::IsTriviallyValid(CValidationState& state) const
{
    if (nVersion == 0 || nVersion > ProUpServPL::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }
    return true;
}

std::string ProUpRegPL::ToString() const
{
    CTxDestination dest;
    std::string payee = ExtractDestination(scriptPayout, dest) ?
                        EncodeDestination(dest) : "unknown";
    return strprintf("ProUpRegPL(nVersion=%d, proTxHash=%s, operatorPubKey=%s, votingAddress=%s, payoutAddress=%s)",
        nVersion, proTxHash.ToString(), bls::EncodePublic(Params(), pubKeyOperator), EncodeDestination(keyIDVoting), payee);
}

void ProUpRegPL::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.pushKV("version", nVersion);
    obj.pushKV("proTxHash", proTxHash.ToString());
    obj.pushKV("votingAddress", EncodeDestination(keyIDVoting));
    CTxDestination dest;
    if (ExtractDestination(scriptPayout, dest)) {
        obj.pushKV("payoutAddress", EncodeDestination(dest));
    }
    obj.pushKV("operatorPubKey", bls::EncodePublic(Params(), pubKeyOperator));
    obj.pushKV("inputsHash", inputsHash.ToString());
}

bool ProUpRegPL::IsTriviallyValid(CValidationState& state) const
{
    if (nVersion == 0 || nVersion > ProUpRegPL::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }
    if (nMode != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-mode");
    }

    if (!pubKeyOperator.IsValid()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-key-invalid");
    }
    if (keyIDVoting.IsNull()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-voting-key-null");
    }
    // !TODO: enable other scripts
    if (!scriptPayout.IsPayToPublicKeyHash()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
    }

    return true;
}

std::string ProUpRevPL::ToString() const
{
    return strprintf("ProUpRevPL(nVersion=%d, proTxHash=%s, nReason=%d)",
                      nVersion, proTxHash.ToString(), nReason);
}

void ProUpRevPL::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.pushKV("version", nVersion);
    obj.pushKV("proTxHash", proTxHash.ToString());
    obj.pushKV("reason", (int)nReason);
    obj.pushKV("inputsHash", inputsHash.ToString());
}

bool ProUpRevPL::IsTriviallyValid(CValidationState& state) const
{
    if (nVersion == 0 || nVersion > ProUpRevPL::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }

    // pl.nReason < ProUpRevPL::REASON_NOT_SPECIFIED is always `false` since
    // pl.nReason is unsigned and ProUpRevPL::REASON_NOT_SPECIFIED == 0
    if (nReason > ProUpRevPL::REASON_LAST) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-reason");
    }
    return true;
}

bool GetProRegCollateral(const CTransactionRef& tx, COutPoint& outRet)
{
    if (tx == nullptr) {
        return false;
    }
    if (!tx->IsSpecialTx() || tx->nType != CTransaction::TxType::PROREG) {
        return false;
    }
    ProRegPL pl;
    if (!GetTxPayload(*tx, pl)) {
        return false;
    }
    outRet = pl.collateralOutpoint.hash.IsNull() ? COutPoint(tx->GetHash(), pl.collateralOutpoint.n)
                                                 : pl.collateralOutpoint;
    return true;
}


