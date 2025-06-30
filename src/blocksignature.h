// Copyright (c) 2017-2021 The PIVX developers
// Copyright (c) 2024 The CLORE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_BLOCKSIGNATURE_H
#define CLORE_BLOCKSIGNATURE_H

#include "key.h"
#include "primitives/block.h"

class CBlock;
class CKeyStore;
class CWallet;

/** Check whether the block signature is valid */
bool CheckBlockSignature(const CBlock& block);

#ifdef ENABLE_WALLET
/** Sign the block */
bool SignBlock(CBlock& block, const CWallet& wallet);
#endif

/** Sign block with a specific key */
bool SignBlockWithKey(CBlock& block, const CKey& key);

/** Get the key ID that signed the block */
bool GetBlockSignerId(const CBlock& block, CKeyID& keyId);

/** Verify block signature with public key */
bool VerifyBlockSignature(const CBlock& block, const CPubKey& pubkey);

//! Check whether the block is a proof-of-stake block
bool IsProofOfStake(const CBlock& block);

//! Get the stake input of a block
bool GetStakeInput(const CBlock& block, CTxIn& txIn);

//! Get the stake output of a block
bool GetStakeOutput(const CBlock& block, CTxOut& txOut);

//! Get the stake value of a block
CAmount GetStakeValue(const CBlock& block);

//! Get the stake age of a block
int64_t GetStakeAge(const CBlock& block);

//! Get the stake modifier of a block
uint64_t GetStakeModifier(const CBlock& block);

//! Get the stake entropy bit of a block
unsigned int GetStakeEntropyBit(const CBlock& block);

//! Get the stake modifier checksum of a block
uint256 GetStakeModifierCheckpoint(const CBlock& block);

//! Check whether the block has a valid stake modifier
bool CheckStakeModifier(const CBlock& block);

//! Check whether the block has a valid stake modifier checksum
bool CheckStakeModifierCheckpoint(const CBlock& block);

#endif // CLORE_BLOCKSIGNATURE_H