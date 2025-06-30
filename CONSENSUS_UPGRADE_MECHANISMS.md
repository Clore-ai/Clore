# CLORE Consensus Upgrade Mechanisms

## Overview

CLORE uses **two different consensus upgrade systems** for different purposes, providing both safety and precision in network upgrades.

## vDeployments (BIP9 Version Bits)

### Purpose

**Miner-signaled soft fork activations** using version bits in block headers

### How it works

- **Bit-based signaling**: Uses specific bits in block version field (e.g., bit 11 for PoS)
- **Miner consensus**: Requires miners to signal readiness by setting version bits
- **Threshold activation**: Activates when enough miners signal (e.g., 75% over 2016 blocks)
- **Time-bounded**: Has start/timeout windows for activation attempts
- **States**: DEFINED → STARTED → LOCKED_IN → ACTIVE (or FAILED)

### Example Configuration

```cpp
// PoS deployment - BIP9 version bits activation
consensus.vDeployments[Consensus::DEPLOYMENT_POS].bit = 11;
consensus.vDeployments[Consensus::DEPLOYMENT_POS].nStartTime = 1719072000;  // Start time
consensus.vDeployments[Consensus::DEPLOYMENT_POS].nTimeout = 1750608000;    // Timeout
consensus.vDeployments[Consensus::DEPLOYMENT_POS].nOverrideRuleChangeActivationThreshold = 999999999; // Threshold
```

## vUpgrades (Height-Based Network Upgrades)

### Purpose

**Direct height-based activations** without requiring miner signaling

### How it works

- **Block height triggers**: Activates automatically at specific block heights
- **No miner signaling**: Doesn't require consensus from miners
- **Immediate activation**: Takes effect exactly at the specified height
- **Simple binary**: Either PENDING, ACTIVE, or DISABLED
- **Protocol version aware**: Can specify minimum protocol versions

### Example Configuration

```cpp
// PoS phases - height-based activation with 1440 block spacing after DEPLOYMENT_POS
consensus.vUpgrades[Consensus::ENABLE_POS_VALIDATORS] = {70002, 1000001439, {}}; // +1440 after DEPLOYMENT_POS (999999999)
consensus.vUpgrades[Consensus::ENABLE_POS_STAKING] = {70002, 1000002879, {}};   // +1440 blocks
consensus.vUpgrades[Consensus::ENABLE_POS_REWARDS] = {70002, 1000004319, {}};   // +1440 blocks
```

## Why CLORE Uses Both Systems

### vDeployments for Initial PoS Signal

- **BIP9 PoS deployment** (bit 11) acts as a **network readiness signal**
- Allows miners to signal when they've upgraded to PoS-capable software
- Currently **disabled** with threshold 999999999 (effectively never activates)
- **Purpose**: Ensures network has upgraded software before enabling PoS

### vUpgrades for Actual PoS Activation

- **Height-based upgrades** control the **actual PoS functionality**
- `ENABLE_POS_VALIDATORS` (1000001439) = validators come online (+1440 after DEPLOYMENT_POS)
- `ENABLE_POS_STAKING` (1000002879) = staking logic activated (+1440 blocks)
- `ENABLE_POS_REWARDS` (1000004319) = Clore PoS (PoW disabled) (+1440 blocks)
- **Purpose**: Precise control over when PoS phases activate

## Implementation Pattern

### Checking Upgrade Status

```cpp
// Check if miners have signaled PoS readiness (BIP9)
bool posSignaled = VersionBitsState(pindex, params, DEPLOYMENT_POS) == THRESHOLD_ACTIVE;

// Check if validators can come online (height-based)
bool validatorsActive = NetworkUpgradeActive(height, params, ENABLE_POS_VALIDATORS);

// Check if staking logic is active (height-based)
bool stakingActive = NetworkUpgradeActive(height, params, ENABLE_POS_STAKING);

// Check if Clore PoS is active (height-based)
bool clorePosActive = NetworkUpgradeActive(height, params, ENABLE_POS_REWARDS);
```

### Configuration Files

- **vDeployments**: Configured in `src/chainparams.cpp` under `consensus.vDeployments[]`
- **vUpgrades**: Configured in `src/chainparams.cpp` under `consensus.vUpgrades[]`

## CLORE Phased PoS Transition

### Transition Strategy: **PoW Security During Infrastructure Buildup**

CLORE uses a **safety-first approach** where PoW mining continues to secure the network while PoS infrastructure builds up gradually. Only when both validators and staking are fully operational does the network transition to PoS-only consensus.

| Phase       | Upgrade                     | Height          | Mining Status  | Infrastructure Status | Purpose                           |
| ----------- | --------------------------- | --------------- | -------------- | --------------------- | --------------------------------- |
| **Phase 0** | _Before all upgrades_       | < 1,000,001,439 | **PoW Only**   | None                  | Traditional PoW mining            |
| **Phase 1** | **`ENABLE_POS_VALIDATORS`** | 1,000,001,439   | **PoW Secure** | Validators Online     | Validators can be created         |
| **Phase 2** | **`ENABLE_POS_STAKING`**    | 1,000,002,879   | **PoW Secure** | + Staking Active      | Staking enabled, PoW still secure |
| **Phase 3** | **`ENABLE_POS_REWARDS`**    | 1,000,004,319   | **PoS Only**   | Full Infrastructure   | PoW disabled, PoS takes over      |

### Technical Implementation Details

| Upgrade                     | Mainnet Height | Technical Changes                                                                                                                                                                                                      | Status      |
| --------------------------- | -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| **`ENABLE_POS_VALIDATORS`** | 1,000,001,439  | **• Validators can be created and come online**<br/>**• PoW mining continues for security**<br/>**• No consensus rule changes**<br/>**• Infrastructure phase begins**                                                  | **Pending** |
| **`ENABLE_POS_STAKING`**    | 1,000,002,879  | **• Staking logic activation (600 block depth)**<br/>**• 256-bit stake modifier activated**<br/>**• PoW mining continues for security**<br/>**• Both PoW and PoS blocks accepted**<br/>**• No consensus rule changes** | **Pending** |
| **`ENABLE_POS_REWARDS`**    | 1,000,004,319  | **• PoW mining permanently disabled**<br/>**• PoW blocks rejected with "pow-disabled" error**<br/>**• PoS-only consensus enforced**<br/>**• ONLY phase with consensus rule changes**                                   | **Pending** |

### **Activation Sequence Analysis**

**Current Order (Mainnet Sequence):**

0. **`DEPLOYMENT_POS`** (999,999,999) - **BIP9 Miner Signaling First**

   - BIP9 version bit 11 signaling for PoS readiness
   - Miners signal software compatibility and network readiness
   - Threshold currently disabled until network coordination
   - No consensus rule changes - signaling only

1. **`ENABLE_POS_VALIDATORS`** (1,000,001,439) - **Validator Infrastructure** (+1440 blocks)

   - Enables validators to come online and establish network
   - Prepares validator infrastructure before staking begins
   - No consensus rule changes - infrastructure only

2. **`ENABLE_POS_STAKING`** (1,000,002,879) - **Staking Logic Activation** (+1440 blocks)

   - Activates staking requirements and logic
   - Implements age → depth based staking with 600 block minimum
   - Upgrades stake modifier to 256-bit for improved security
   - No consensus rule changes - staking infrastructure only

3. **`ENABLE_POS_REWARDS`** (1,000,004,319) - **Clore PoS Enforcement** (+1440 blocks)
   - **ONLY phase with consensus rule changes**
   - Permanently disables PoW mining and block acceptance
   - Enforces Clore PoS consensus with "pow-disabled" rejection
   - Completes transition to full PoS network

### **Network-Specific Configurations**

| Network     | DEPLOYMENT_POS | VALIDATOR     | STAKING       | REWARDS       | Spacing         | Strategy                |
| ----------- | -------------- | ------------- | ------------- | ------------- | --------------- | ----------------------- |
| **Mainnet** | 999,999,999    | 1,000,001,439 | 1,000,002,879 | 1,000,004,319 | **1440 blocks** | **Production sequence** |
| **Testnet** | N/A            | 1,000         | 2,440         | 3,880         | **1440 blocks** | **Proper testing**      |
| **Regtest** | N/A            | 100           | 150           | 200           | **50 blocks**   | **Rapid testing**       |

### **Code Implementation References**

- **vDeployments**: `src/chainparams.cpp` lines 165-169, 373-377, 569-573
- **vUpgrades**: `src/chainparams.cpp` lines 194-197, 396-399, 592-595
- **Usage Functions**: `src/consensus/params.h` lines 183-210
- **Validation Logic**: `src/validation.cpp` (PoS validation)
- **Mining Logic**: `src/miner.cpp` (PoW/PoS block creation)
- **Staking Logic**: `src/kernel.h` (stake modifier algorithms)

## Key Differences Summary

| Aspect                | vDeployments (BIP9)             | vUpgrades (Height-based) |
| --------------------- | ------------------------------- | ------------------------ |
| **Activation Method** | Miner signaling                 | Block height             |
| **Timing**            | Variable (depends on signaling) | Exact height             |
| **Miner Consensus**   | Required                        | Not required             |
| **Rollback**          | Possible (if not locked in)     | Not possible             |
| **Complexity**        | High (state machine)            | Low (simple check)       |
| **Use Case**          | Software readiness              | Feature activation       |

## Files to Review

### Core Implementation

- `src/consensus/params.h` - Upgrade definitions
- `src/chainparams.cpp` - Network configurations
- `src/consensus/upgrades.cpp` - Height-based upgrade logic
- `src/versionbits.cpp` - BIP9 version bits logic

### Usage Examples

- `src/miner.cpp` - Mining logic checks
- `src/validation.cpp` - Consensus validation
- `src/rpc/staking.cpp` - RPC status reporting

## Future Considerations

1. **PoS Activation Strategy**: Decide whether to use BIP9 signaling or direct height activation
2. **Threshold Management**: Adjust BIP9 thresholds when ready for mainnet activation
3. **Coordination**: Ensure both systems are aligned for smooth transition
4. **Monitoring**: Track both deployment states during activation period

This dual mechanism provides CLORE with maximum flexibility and safety for the critical PoS transition while maintaining compatibility with Bitcoin-style consensus upgrades.
