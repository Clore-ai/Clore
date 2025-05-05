Clore Core
==================================

https://Blockchain.clore.ai

What is Clore?
-----------------
Clore is a blockchain used for rewarding hosting providers on clore.ai marketplace (https://clore.ai/marketplace). 


# 🧪 Alpha Release: Dual Consensus – PoW + PoS (Experimental)

We’re excited to share the first **alpha release** of Clore’s Dual Consensus system, introducing **Proof of Stake (PoS)** alongside our existing **Proof of Work (PoW)** mechanism.

This is an **early-stage, experimental release** intended for testing, development, and community feedback. Expect bugs, instability, and incomplete features as we continue development.

---

## 🔍 Overview

This alpha release lays the groundwork for a **hybrid consensus mechanism**, allowing Clore to transition toward a more secure, energy-efficient, and inclusive blockchain protocol.

---

## ⚙️ Key Features (Alpha)

### 🧱 1. PoS Consensus Engine (Experimental)
- Initial implementation of PoS block validation and reward logic.  
- Operates in isolation for testnet purposes.  
- Not yet optimized for performance.

### 🔁 2. Dual Consensus Support
- Proof of Work and Proof of Stake can **coexist**.  
- Nodes can now validate blocks from either consensus mechanism.  
- Switching between PoW and PoS is enabled but may encounter edge-case bugs.

### 🔐 3. Staking Functionality (Prototype)
- **Cold Staking**: Stake using a Hot Wallet while keeping CLORE offline.  
- **Hot Staking**: Stake directly with an always-online wallet.  
- Hardware wallet support not included yet.

---

## ⚠️ Alpha Disclaimer

> 🛠 This software is under active development.  
> ❗ **Do not use this release in production.**  
> ❗ **Testnet CLORE coins staked or mined in this alpha may not be secure.**  
> 🐞 Bugs and issues are expected – we encourage testing and feedback!

---

## 🔍 What to Test

- Staking wallet setup (hot and cold)  
- Staking selection and reward logic  
- Network synchronization under each consensus state
- Edge cases around staking delegation and block validation

---

## Thank you to the following:

- Bitcoin developers
- Ravencoin developers
- Neoxa developers
- Pivx developers
