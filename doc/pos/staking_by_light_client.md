# CLORE Staking Guide - Light Client (Cold Staking)

## Overview
This guide covers light client staking using CLORE's cold staking feature. This method allows users to stake their CLORE without running a full node by delegating staking rights to authorized validators while retaining full spending control of their coins.

## Prerequisites
- Light wallet (mobile app, web wallet, or thin client)
- Internet connection
- CLORE coins to stake
- Basic understanding of cryptocurrency transactions

---

## Step 1: Install CLORE Light Wallet

### Mobile Wallet (Recommended for beginners)
```
1. Download CLORE Mobile Wallet from:
   - iOS App Store: "CLORE Wallet"
   - Google Play Store: "CLORE Wallet"
   - APK Download: wallet.clore.ai

2. Install and launch the app
3. Choose "Create New Wallet" or "Import Existing Wallet"
```

### Web Wallet
```
1. Visit: wallet.clore.ai
2. Click "Create New Wallet"
3. Follow security setup instructions
4. Bookmark the URL for future access
```

### Desktop Light Client
```
# Download light client from official website
1. Visit: clore.ai/downloads
2. Download "CLORE Light Client" for your OS
3. Install following standard procedures
4. Launch application
```

---

## Step 2: Create and Secure Your Wallet

### Create New Wallet
```
1. Choose "Create New Wallet"
2. Write down your 12/24-word recovery phrase
3. Verify the recovery phrase by entering words in order
4. Set a strong password for the wallet
5. Complete wallet creation
```

### Import Existing Wallet (if applicable)
```
1. Choose "Import Wallet" or "Restore from Seed"
2. Enter your 12/24-word recovery phrase
3. Set a new password for this device
4. Wait for wallet to sync (usually under 1 minute)
```

### Secure Your Wallet
```
📝 CRITICAL SECURITY STEPS:
✅ Write down recovery phrase on paper (never digital)
✅ Store recovery phrase in secure location
✅ Test recovery phrase before funding wallet
✅ Set strong, unique password
✅ Enable biometric authentication if available
✅ Never share recovery phrase with anyone
```

---

## Step 3: Get Your Receiving Address

### Generate Receiving Address
```
1. Open your wallet
2. Navigate to "Receive" or "Receive CLORE"
3. Copy your wallet address
4. Optional: Generate QR code for easy sharing

Example address format: AJl8h2T3WeZ6aFgNp7YZJJrn2xwKzAUFAcE
```

### Verify Address
```
✅ Double-check address format (starts with 'A')
✅ Verify all characters are correct
✅ Use QR code when possible to avoid typos
✅ Test with small amount first
```

---

## Step 4: Acquire CLORE Coins

### Purchase CLORE
```
1. Exchanges supporting CLORE:
   - [List current exchanges]
   - Check coinmarketcap.com for updated list

2. Purchase process:
   - Create exchange account
   - Complete KYC if required
   - Deposit fiat or other crypto
   - Buy CLORE
   - Withdraw to your wallet address
```

### Other Methods
```
- Mining pools (if PoW phase active)
- Peer-to-peer trading
- Community airdrops/rewards
- Cross-chain bridges (if available)
```

### Verify Receipt
```
1. Check wallet balance in app
2. Wait for confirmations (usually 1-6 blocks)
3. Transaction should appear in "Recent Transactions"
4. Balance should update automatically
```

---

## Step 5: Find Authorized Validators

### Check Available Validators
```
Option A: In-Wallet Validator List
1. Navigate to "Staking" or "Cold Staking" section
2. View "Available Validators" list
3. See validator details (name, fee, uptime)

Option B: Community Resources
1. Visit: validators.clore.ai
2. Check CLORE community Discord
3. Review validator announcements
```

### Validator Selection Criteria
```
Consider these factors when choosing:
✅ Commission/Fee Rate (typically 5-15%)
✅ Uptime/Reliability (check historical performance)
✅ Community Reputation
✅ Geographic Distribution
✅ Technical Infrastructure
✅ Communication/Support
```

### Current Authorized Validators (Example)
```
Validator Name: CLORE Validator 01
Address: AN8h7T3WeZ6aFgNp7YZJJrn2xwKzAUFAcE
Fee: 10%
Uptime: 99.8%
Location: US-East

Validator Name: CLORE Validator 02  
Address: AJH5d9s1Jd2xW8nKe4bFgN7pL3mR9vT2cX
Fee: 8%
Uptime: 99.9%
Location: EU-West

(Use the most current list from your wallet or clore.ai)
```

---

## Step 6: Delegate to Validator (Cold Staking)

### Using Mobile/Web Wallet
```
1. Navigate to "Staking" or "Cold Staking" section
2. Click "Delegate for Staking" or "Start Staking"
3. Choose validator from authorized list
4. Enter amount to delegate (minimum: 1 CLORE)
5. Review transaction details:
   - Validator address
   - Amount
   - Estimated fees
   - Your retained control
6. Confirm delegation
7. Wait for transaction confirmation
```

### Using Light Client CLI (Advanced)
```bash
# If your light client supports CLI commands
clore-light-cli listauthorizedvalidators

# Delegate to chosen validator
clore-light-cli delegateforstaking "AN8h7T3WeZ6aFgNp7YZJJrn2xwKzAUFAcE" 100.0

# Check delegation status
clore-light-cli getcoldstakinginfo
```

### Delegation Transaction Details
```
What happens when you delegate:
✅ Your CLORE moves to a cold staking script
✅ Validator can stake with your coins
✅ You retain full spending control
✅ Validator CANNOT spend your coins
✅ You can undelegate anytime
✅ Staking rewards are shared per agreement
```

---

## Step 7: Monitor Your Cold Staking

### Check Delegation Status
```
1. Open wallet and navigate to "Staking" section
2. View "My Delegations" or "Active Stakes"
3. Check status of each delegation:
   - Amount delegated
   - Validator name/address
   - Duration
   - Estimated returns
   - Current status (Active/Pending)
```

### Monitor Rewards
```
1. Check "Staking Rewards" or "Earnings" section
2. Track received rewards over time
3. Verify validator performance
4. Compare actual vs estimated returns

Rewards typically appear as:
- Regular small amounts
- Proportional to your delegation
- Shared with validator (minus their fee)
```

### Track Performance
```
Weekly monitoring:
✅ Check delegation status (should show "Active")
✅ Verify validator uptime
✅ Review earned rewards
✅ Compare with expected returns
✅ Monitor validator reputation
```

---

## Step 8: Managing Your Delegations

### Undelegate (Withdraw from Staking)
```
If you need to undelegate:
1. Navigate to "My Delegations"
2. Select delegation to withdraw
3. Click "Undelegate" or "Withdraw"
4. Confirm transaction
5. Wait for confirmation (usually 1-10 minutes)
6. Coins return to your regular balance
```

### Switch Validators
```
To change validators:
1. Undelegate from current validator
2. Wait for coins to return to regular balance
3. Delegate to new preferred validator
4. Monitor new delegation status

Note: Brief gap in staking during switch
```

### Partial Undelegation
```
To reduce delegation amount:
1. Undelegate entire amount
2. Re-delegate desired amount
3. Keep remaining in regular balance

(Some wallets may support direct partial undelegation)
```

---

## Troubleshooting Common Issues

### Delegation Not Showing as Active
```
Possible causes and solutions:
- Transaction still confirming → Wait for more confirmations
- Validator offline → Check validator status, consider switching
- Insufficient amount → Ensure meeting minimum delegation
- Wrong validator → Verify using authorized validator
```

### Low or No Staking Rewards
```
Check these factors:
- Validator performance/uptime
- Network staking difficulty
- Your delegation amount relative to total
- Validator fee structure
- Time since delegation (rewards take time)
```

### Unable to Connect to Network
```
Solutions:
- Check internet connection
- Restart wallet application
- Update to latest version
- Try different network (WiFi/Mobile data)
- Contact support if persistent
```

### Wallet Sync Issues
```
If wallet won't sync:
- Force refresh/resync in settings
- Clear cache (if available)
- Reinstall wallet app
- Check official status page for network issues
```

---

## Security Best Practices

### Wallet Security
```
✅ Keep recovery phrase secure and offline
✅ Use strong, unique password
✅ Enable 2FA/biometric authentication
✅ Keep wallet app updated
✅ Only download official wallet versions
✅ Never share private keys/recovery phrase
```

### Delegation Security
```
✅ Only delegate to authorized validators
✅ Verify validator addresses carefully
✅ Start with small amounts initially
✅ Monitor validator performance regularly
✅ Diversify across multiple validators if possible
✅ Keep some CLORE undelegated for liquidity
```

### Device Security
```
✅ Use secure, updated devices
✅ Avoid public WiFi for transactions
✅ Enable device lock screens
✅ Regular device security updates
✅ Use reputable antivirus software
✅ Be cautious of phishing attempts
```

---

## Understanding Cold Staking Economics

### How Rewards Work
```
Reward Distribution:
1. Validator stakes and earns block rewards
2. Validator takes commission (5-15% typically)
3. Remaining rewards distributed to delegators
4. Distribution proportional to delegation amount
5. Rewards appear in your wallet automatically
```

### Expected Returns
```
Factors affecting returns:
- Total network staking participation
- Your delegation amount
- Validator performance and uptime
- Validator commission rate
- Network reward rate

Typical annual returns: 5-15% (varies by conditions)
```

### Fee Structure
```
Costs involved:
- Transaction fees for delegation (~0.001 CLORE)
- Transaction fees for undelegation (~0.001 CLORE)
- Validator commission (5-15% of rewards)
- No ongoing fees for holding delegation
```

---

## Comparing Light Client vs Full Node Staking

| Feature | Light Client | Full Node |
|---------|-------------|-----------|
| **Setup Complexity** | ✅ Very Easy | ❌ Technical |
| **Hardware Requirements** | ✅ Any device | ❌ Dedicated PC |
| **Always Online** | ✅ No requirement | ❌ Must be 24/7 |
| **Technical Knowledge** | ✅ Minimal | ❌ Advanced |
| **Maintenance** | ✅ None | ❌ Regular |
| **Control Level** | ⚠️ Shared with validator | ✅ Complete |
| **Returns** | ⚠️ After validator fees | ✅ Full rewards |
| **Network Support** | ⚠️ Relies on validators | ✅ Direct support |

---

## Advanced Features

### Multi-Validator Delegation
```
For larger amounts, consider:
1. Split delegations across multiple validators
2. Reduce risk of single validator issues
3. Compare performance across validators
4. Optimize for different validator strengths

Example split:
- 40% to highest-uptime validator
- 30% to lowest-fee validator  
- 30% to geographically diverse validator
```

### Automated Management
```
Some wallets offer:
- Auto-compounding of rewards
- Automatic validator switching
- Performance-based rebalancing
- Scheduled delegation/undelegation
```

### Portfolio Tracking
```
Track your staking portfolio:
- Total delegated amount
- Active validators and their performance
- Historical rewards earned
- Projected annual returns
- Comparison with holding unstaked
```

---

## Getting Help and Support

### Wallet Support
```
- In-app help sections
- Official documentation: docs.clore.ai
- Video tutorials on YouTube
- Community Discord server
- Email support (if available)
```

### Validator Issues
```
- Contact validator directly (if they provide support)
- Post in community forums
- Switch to different validator
- Report issues to CLORE team
```

### Technical Support
```
Before seeking help, gather:
- Wallet version and device info
- Transaction IDs (if relevant)
- Screenshots of error messages
- Steps to reproduce issues
- Validator address(es) involved
```

---

## Conclusion

Light client cold staking offers an excellent balance of:

### Advantages
- ✅ **Easy Setup**: No technical expertise required
- ✅ **Low Maintenance**: Set and forget staking
- ✅ **Device Freedom**: Use any smartphone/computer
- ✅ **Energy Efficient**: No always-on requirements
- ✅ **Security**: Retain spending control of your coins
- ✅ **Flexibility**: Undelegate anytime

### Considerations
- ⚠️ **Validator Dependency**: Performance depends on chosen validator
- ⚠️ **Reduced Returns**: Validator fees reduce overall returns
- ⚠️ **Trust Element**: Must trust validator for good performance
- ⚠️ **Limited Validators**: Only authorized validators available

Cold staking is ideal for users who want to:
- Earn staking rewards without technical complexity
- Stake from mobile devices or light wallets
- Avoid running always-on hardware
- Maintain flexibility and control over their funds

For users comfortable with technical requirements and wanting maximum control and returns, see the [Command Line Client Staking Guide](staking_by_command_client.md).

---

**⚠️ Important Disclaimer**: 
Cold staking involves delegating staking rights while retaining spending control. Choose validators carefully and only delegate amounts you're comfortable with. This guide is educational and not financial advice. Always do your own research and consider consulting financial professionals before making investment decisions.
