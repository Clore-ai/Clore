// Copyright (c) 2017-2021 The PIVX developers
// Copyright (c) 2024 The CLORE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_STAKEINPUT_H
#define CLORE_STAKEINPUT_H

#include "amount.h"
#include "chain.h"
#include "streams.h"
#include "uint256.h"

class CBlock;
class CBlockIndex;
class CTxIn;
class CTxOut;
class CTransaction;
class CWallet;
class CWalletTx;

class CStakeInput
{
protected:
    const CBlockIndex* pindexFrom = nullptr;

public:
    virtual ~CStakeInput() {};
    virtual const CBlockIndex* GetIndexFrom() const = 0;
    virtual bool GetTxOutFrom(CTxOut& out) const = 0;
    virtual CAmount GetValue() const = 0;
    virtual bool CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut = uint256()) = 0;
    virtual bool GetModifier(uint64_t& nStakeModifier) const = 0;
    virtual bool IsZerocoinSpend() const { return false; }
    virtual CDataStream GetUniqueness() const = 0;
    virtual bool ContextCheck(int nHeight, uint32_t nTime) = 0;
};

class CCloreStake : public CStakeInput
{
private:
    const CTransaction* txFrom;
    unsigned int nPosition;
    const CBlockIndex* pindexFrom;

public:
    CCloreStake() : txFrom(nullptr), nPosition(0), pindexFrom(nullptr) {}
    CCloreStake(const CTxOut& txOut, const COutPoint& outPoint, const CBlockIndex* pindex);

    static CCloreStake* NewCloreStake(const CTxIn& txin, int nHeight, uint32_t nTime);

    bool SetInput(const CTransaction* txPrev, unsigned int n);

    const CBlockIndex* GetIndexFrom() const override;
    bool GetTxOutFrom(CTxOut& out) const override;
    CAmount GetValue() const override;
    bool CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut = uint256()) override;
    bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal);
    bool GetModifier(uint64_t& nStakeModifier) const override;
    CDataStream GetUniqueness() const override;
    bool ContextCheck(int nHeight, uint32_t nTime) override;
    bool IsValid() const;
    CTxIn GetTxIn() const;
    bool GetStakeKernelHash(uint256& hashRet) const;

    const CTransaction* GetTxFrom() const { return txFrom; }
    unsigned int GetPosition() const { return nPosition; }
};

// Forward declaration - defined in wallet/wallet.h
class CStakeableOutput;

#endif // CLORE_STAKEINPUT_H