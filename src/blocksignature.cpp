// Copyright (c) 2017-2021 The PIVX developers
// Copyright (c) 2024 The CLORE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blocksignature.h"

#include "chain.h"
#include "key.h"
#include "keystore.h"
#include "primitives/block.h"
#include "script/standard.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

bool CheckBlockSignature(const CBlock& block)
{
    if (block.IsProofOfWork()) {
        return true;
    }

    if (block.vchBlockSig.empty()) {
        return error("CheckBlockSignature(): block signature is empty");
    }

    // Get the coinstake transaction
    if (block.vtx.size() < 2) {
        return error("CheckBlockSignature(): POS block missing coinstake transaction");
    }

    const CTransaction& coinstake = *block.vtx[1];
    if (coinstake.vout.empty()) {
        return error("CheckBlockSignature(): coinstake has no outputs");
    }

    // Get the public key from the second output (first is empty)
    if (coinstake.vout.size() < 2) {
        return error("CheckBlockSignature(): coinstake needs at least 2 outputs");
    }

    const CTxOut& txout = coinstake.vout[1];

    // Extract public key from script
    std::vector<std::vector<unsigned char>> vSolutions;
    txnouttype whichType;
    if (!Solver(txout.scriptPubKey, whichType, vSolutions)) {
        return error("CheckBlockSignature(): failed to parse coinstake output script");
    }

    CPubKey pubkey;
    if (whichType == TX_PUBKEY) {
        pubkey = CPubKey(vSolutions[0]);
    } else if (whichType == TX_PUBKEYHASH) {
        // For P2PKH, we need to find the pubkey from the signature
        // This is more complex and would require looking at the input
        return error("CheckBlockSignature(): P2PKH not supported for block signing");
    } else {
        return error("CheckBlockSignature(): unsupported output type for block signing");
    }

    if (!pubkey.IsValid()) {
        return error("CheckBlockSignature(): invalid public key");
    }

    return VerifyBlockSignature(block, pubkey);
}

#ifdef ENABLE_WALLET
bool SignBlock(CBlock& block, const CWallet& wallet)
{
    if (block.IsProofOfWork()) {
        return true;
    }

    // Get the coinstake transaction
    if (block.vtx.size() < 2) {
        return error("SignBlock(): POS block missing coinstake transaction");
    }

    const CTransaction& coinstake = *block.vtx[1];
    if (coinstake.vout.size() < 2) {
        return error("SignBlock(): coinstake needs at least 2 outputs");
    }

    const CTxOut& txout = coinstake.vout[1];

    // Extract the destination from the script
    CTxDestination dest;
    if (!ExtractDestination(txout.scriptPubKey, dest)) {
        return error("SignBlock(): failed to extract destination from coinstake output");
    }

    const CKeyID* keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        return error("SignBlock(): destination is not a key ID");
    }

    // Get the private key from wallet
    CKey key;
    if (!wallet.GetKey(*keyID, key)) {
        return error("SignBlock(): wallet does not have key for signing");
    }

    return SignBlockWithKey(block, key);
}
#endif // ENABLE_WALLET

bool SignBlockWithKey(CBlock& block, const CKey& key)
{
    if (block.IsProofOfWork()) {
        return true;
    }

    // Create the hash to sign (block hash without signature)
    CBlock blockCopy = block;
    blockCopy.vchBlockSig.clear();
    uint256 hashToSign = blockCopy.GetHash();

    // Sign the hash
    std::vector<unsigned char> vchSig;
    if (!key.Sign(hashToSign, vchSig)) {
        return error("SignBlockWithKey(): failed to sign block hash");
    }

    // Set the signature
    block.vchBlockSig = vchSig;

    return true;
}

bool GetBlockSignerId(const CBlock& block, CKeyID& keyId)
{
    if (block.IsProofOfWork()) {
        return false;
    }

    if (block.vtx.size() < 2) {
        return false;
    }

    const CTransaction& coinstake = *block.vtx[1];
    if (coinstake.vout.size() < 2) {
        return false;
    }

    const CTxOut& txout = coinstake.vout[1];

    // Extract the destination from the script
    CTxDestination dest;
    if (!ExtractDestination(txout.scriptPubKey, dest)) {
        return false;
    }

    const CKeyID* pKeyID = boost::get<CKeyID>(&dest);
    if (!pKeyID) {
        return false;
    }

    keyId = *pKeyID;
    return true;
}

bool VerifyBlockSignature(const CBlock& block, const CPubKey& pubkey)
{
    if (block.IsProofOfWork()) {
        return true;
    }

    if (block.vchBlockSig.empty()) {
        return false;
    }

    // Create the hash that was signed (block hash without signature)
    CBlock blockCopy = block;
    blockCopy.vchBlockSig.clear();
    uint256 hashToVerify = blockCopy.GetHash();

    // Verify the signature
    return pubkey.Verify(hashToVerify, block.vchBlockSig);
}

bool IsProofOfStake(const CBlock& block)
{
    return block.IsProofOfStake();
}

bool GetStakeInput(const CBlock& block, CTxIn& txIn)
{
    if (!block.IsProofOfStake())
        return false;

    txIn = block.vtx[1]->vin[0];
    return true;
}

bool GetStakeOutput(const CBlock& block, CTxOut& txOut)
{
    if (!block.IsProofOfStake())
        return false;

    txOut = block.vtx[1]->vout[1];
    return true;
}

CAmount GetStakeValue(const CBlock& block)
{
    if (!block.IsProofOfStake())
        return 0;

    return block.vtx[1]->vout[1].nValue;
}

int64_t GetStakeAge(const CBlock& block)
{
    if (!block.IsProofOfStake())
        return 0;

    // Simplified implementation - assume 1 hour stake age
    return 3600;
}

uint64_t GetStakeModifier(const CBlock& block)
{
    if (!block.IsProofOfStake())
        return 0;

    // Simplified implementation - use block hash
    return block.GetHash().GetUint64(0);
}

unsigned int GetStakeEntropyBit(const CBlock& block)
{
    if (!block.IsProofOfStake())
        return 0;

    // Simplified implementation - use lowest bit of block hash
    return block.GetHash().GetUint64(0) & 1;
}

uint256 GetStakeModifierCheckpoint(const CBlock& block)
{
    if (!block.IsProofOfStake())
        return uint256();

    // Simplified implementation - return block hash
    return block.GetHash();
}

bool CheckStakeModifier(const CBlock& block)
{
    if (!block.IsProofOfStake())
        return true;

    // Simplified implementation - always valid for non-empty blocks
    return !block.vtx.empty();
}

bool CheckStakeModifierCheckpoint(const CBlock& block)
{
    if (!block.IsProofOfStake())
        return true;

    // Simplified implementation - check if block hash is not null
    return !block.GetHash().IsNull();
}