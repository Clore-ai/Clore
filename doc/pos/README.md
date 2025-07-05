# CLORE Proof of Stake (PoS) Documentation

This directory contains comprehensive guides for participating in CLORE's Proof of Stake consensus mechanism.

## Available Staking Methods

### 🖥️ [Command Line Client Staking](staking_by_command_client.md)
**Traditional Full Node Staking**
- Requires running a full CLORE node
- Complete control over staking process
- Higher technical requirements
- Maximum security and returns
- Suitable for: Technical users, dedicated hardware setups

### 📱 [Light Client Staking](staking_by_light_client.md) 
**Cold Staking with Validator Delegation**
- Works with mobile/web wallets
- Delegate to authorized validators
- No full node required
- User-friendly and accessible
- Suitable for: General users, mobile devices

## Quick Comparison

| Feature | Command Client | Light Client |
|---------|---------------|--------------|
| **Ease of Use** | ❌ Technical | ✅ User-friendly |
| **Hardware Needs** | ❌ Dedicated PC | ✅ Any device |
| **Always Online** | ❌ Required | ✅ Not required |
| **Setup Time** | ❌ Hours | ✅ Minutes |
| **Maintenance** | ❌ Regular | ✅ Minimal |
| **Control** | ✅ Complete | ⚠️ Shared |
| **Returns** | ✅ Full rewards | ⚠️ After fees |
| **Security** | ✅ Maximum | ✅ High |

## Getting Started

### For Beginners
👉 **Start with [Light Client Staking](staking_by_light_client.md)**
- Download CLORE mobile wallet
- Create wallet and get CLORE
- Choose authorized validator
- Delegate and start earning

### For Advanced Users
👉 **Use [Command Line Client Staking](staking_by_command_client.md)**
- Download CLORE Core
- Sync full blockchain
- Set up secure staking node
- Earn full staking rewards

## CLORE PoS Phases

CLORE's Proof of Stake rollout happens in phases:

- **Phase 0**: Pure PoW (mining only)
- **Phase 1**: Validator infrastructure setup
- **Phase 2**: Staking active (PoW + PoS hybrid)
- **Phase 3**: Pure PoS (staking only)

Both staking methods become active in Phase 2.

## Security Notes

### ⚠️ Important Reminders
- Only delegate to **authorized validators** (for light client staking)
- Always backup your wallet and recovery phrases
- Use strong, unique passwords
- Keep software updated
- Never share private keys or recovery phrases
- Start with small amounts to test

### 🛡️ Authorized Validators
Cold staking (light client) only works with validators approved by CLORE:
- Check current list: `clore-cli listauthorizedvalidators`
- Verify validator authorization before delegating
- Unauthorized addresses will be rejected

## Support and Resources

- **Documentation**: TBD
- **Community**: CLORE Discord Server
- **GitHub**: [github.com/clore-ai/clore](https://github.com/clore-ai/clore)
- **Website**: [clore.ai](https://clore.ai)

## Contributing

Found an error or want to improve these guides?
- Submit issues or pull requests on GitHub
- Discuss improvements in the community Discord
- Help translate guides to other languages