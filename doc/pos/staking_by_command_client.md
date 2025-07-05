# CLORE Staking Guide - Command Line Client (Full Node)

## Overview
This guide covers traditional staking using the CLORE command line client with a full node. This method requires running a complete blockchain node and is suitable for users comfortable with command line interfaces.

## Prerequisites
- Computer with adequate storage (50GB+ recommended)
- Stable internet connection
- Basic command line knowledge
- Sufficient CLORE for staking (minimum varies by network conditions)

---

## Step 1: Install CLORE Core

### Download and Install
```bash
# Download the latest CLORE Core release
# Visit https://github.com/clore-ai/clore/releases for latest version
wget https://github.com/clore-ai/clore/releases/latest/clore-core.tar.gz

# Extract the files
tar -xzf clore-core.tar.gz

# Move binaries to system path (Linux/Mac)
sudo cp clore-core/bin/* /usr/local/bin/

# Verify installation
clore_blockchaind --version
clore-cli --version
```

### Initial Configuration
```bash
# Create data directory
mkdir -p ~/.clore

# Create basic configuration file
cat > ~/.clore/clore.conf << 'CONF'
# Basic CLORE Configuration
server=1
daemon=1
rpcuser=cloreuser
rpcpassword=your_secure_password_here
rpcallowip=127.0.0.1
listen=1
staking=1

# Network settings
maxconnections=50
addnode=seed1.clore.ai
addnode=seed2.clore.ai
addnode=seed3.clore.ai
CONF
```

---

## Step 2: Start Full Node and Sync Blockchain

### Start the Daemon
```bash
# Start CLORE daemon
clore_blockchaind -daemon

# Check if daemon is running
clore-cli getinfo
```

### Monitor Synchronization
```bash
# Check sync progress
clore-cli getblockchaininfo

# Check current block height vs network height
echo "Local blocks: $(clore-cli getblockcount)"
echo "Network info: $(clore-cli getblockchaininfo | grep -E 'blocks|verificationprogress')"
```

**⏰ Sync Time**: Initial blockchain sync can take 2-8 hours depending on your internet speed and hardware.

### Verify Full Sync
```bash
# Check if fully synced
clore-cli getblockchaininfo | grep verificationprogress
# Should show close to 1.0 when fully synced

# Verify connections to network
clore-cli getconnectioncount
# Should show multiple peer connections
```

---

## Step 3: Create and Secure Wallet

### Create New Wallet
```bash
# Create a new wallet (if not already created)
clore-cli createwallet "staking_wallet" || echo "Using existing wallet"

# Generate a new receiving address
clore-cli getnewaddress "staking_address"

# Get your wallet info
clore-cli getwalletinfo
```

### Secure Your Wallet
```bash
# Encrypt wallet with strong passphrase
clore-cli encryptwallet "your_very_secure_passphrase"

# Note: Daemon will shutdown after encryption, restart it
clore_blockchaind -daemon

# Backup wallet (CRITICAL - store securely!)
cp ~/.clore/wallet.dat ~/clore_wallet_backup_$(date +%Y%m%d).dat
```

**🔐 Security Note**: Store your backup and passphrase in separate, secure locations!

---

## Step 4: Acquire CLORE Coins

### Get Your Receiving Address
```bash
# Generate fresh address for receiving CLORE
RECEIVING_ADDRESS=$(clore-cli getnewaddress "staking")
echo "Send CLORE to this address: $RECEIVING_ADDRESS"
```

### Acquire CLORE Methods
1. **Exchange Purchase**: Buy CLORE on supported exchanges
2. **Mining**: Participate in CLORE mining (during PoW phases)
3. **Trading**: Exchange other cryptocurrencies for CLORE
4. **Community**: Participate in community rewards/airdrops

### Verify Receipt
```bash
# Check wallet balance
clore-cli getbalance

# List recent transactions
clore-cli listtransactions "" 10

# Check specific address balance
clore-cli getreceivedbyaddress "$RECEIVING_ADDRESS"
```

---

## Step 5: Prepare for Staking

### Verify PoS Activation Status
```bash
# Check current PoS information
clore-cli getposinfo

# Check staking info
clore-cli getstakinginfo
```

### Check Coin Maturity
```bash
# Check unspent transactions (must be 120+ confirmations)
clore-cli listunspent | grep -A1 -B1 confirmations

# Only mature coins can participate in staking
```

### Verify Network Phase
```bash
# Check current blockchain info for PoS status
clore-cli getblockchaininfo

# Staking is only active in Phase 2+ of PoS rollout
# Phase 0: Pure PoW (staking not active)
# Phase 1: Validators preparation
# Phase 2: Staking active (PoW + PoS)
# Phase 3: Pure PoS only
```

---

## Step 6: Enable Staking

### Unlock Wallet for Staking
```bash
# Unlock wallet for staking only (most secure)
clore-cli walletpassphrase "your_passphrase" 0 true

# Alternative: Unlock for specific time (less secure)
# clore-cli walletpassphrase "your_passphrase" 3600
```

### Enable Staking
```bash
# Enable staking
clore-cli setstaking true

# Verify staking is enabled and working
clore-cli getstakinginfo
```

### Expected Staking Output
```json
{
  "enabled": true,
  "staking": true,
  "errors": "",
  "currentblocksize": 1000,
  "currentblocktx": 0,
  "pooledtx": 0,
  "difficulty": 1.23456789,
  "search-interval": 16,
  "weight": 50000000000,
  "expected_time": 3600
}
```

---

## Step 7: Monitor and Maintain Staking

### Daily Monitoring
```bash
# Check staking status
clore-cli getstakinginfo

# Verify wallet is still unlocked
clore-cli getwalletinfo | grep unlocked_until

# Check balance for new staking rewards
clore-cli getbalance

# Monitor recent transactions
clore-cli listtransactions "" 5
```

### Weekly Maintenance
```bash
# Backup wallet if balance changed significantly
cp ~/.clore/wallet.dat ~/backups/wallet_$(date +%Y%m%d).dat

# Check for software updates
clore-cli getnetworkinfo

# Verify peer connections
clore-cli getconnectioncount
```

### Performance Monitoring
```bash
# Check your staking weight vs network
clore-cli getposinfo

# Calculate estimated time to find next block
# Formula: (network_weight / your_weight) * average_block_time

# Review staking history
clore-cli listtransactions "" 50 | grep -A5 -B5 "category.*generate"
```

---

## Troubleshooting Common Issues

### Staking Not Active
```bash
# Check wallet unlock status
clore-cli getwalletinfo | grep unlocked

# Verify staking is enabled
clore-cli getstakinginfo | grep enabled

# Check if coins are mature enough
clore-cli listunspent | grep confirmations
```

### Low Staking Weight
```bash
# Check available balance
clore-cli getbalance

# Check coin age (older coins have more weight)
clore-cli listunspent

# Consider consolidating small UTXOs
# clore-cli sendtoaddress YOUR_ADDRESS $(clore-cli getbalance) "" "" true
```

### Connection Issues
```bash
# Check peer connections
clore-cli getpeerinfo | grep addr

# Add more seed nodes if needed
clore-cli addnode "node.clore.ai:8787" "add"

# Check network status
clore-cli getnetworkinfo
```

### Synchronization Problems
```bash
# Check sync progress
clore-cli getblockchaininfo | grep -E "(blocks|headers|verificationprogress)"

# Restart daemon if stuck
clore-cli stop
sleep 5
clore_blockchaind -daemon
```

---

## Security Best Practices

### Wallet Security
- ✅ **Always encrypt your wallet** with a strong passphrase
- ✅ **Regular backups** of wallet.dat file
- ✅ **Store backups securely** in multiple locations
- ✅ **Use unique, strong passphrases** (20+ characters)
- ✅ **Keep software updated** to latest versions

### Operational Security
- ✅ **Dedicated hardware** for staking if possible
- ✅ **Firewall configuration** to limit unnecessary access
- ✅ **Monitor system logs** for unusual activity
- ✅ **Regular OS updates** and security patches
- ✅ **Backup automation** for critical files

### Network Security
- ✅ **Trusted nodes only** for connections
- ✅ **Verify checksums** before software installation
- ✅ **VPN usage** if additional privacy needed
- ✅ **Monitor network traffic** for anomalies

---

## Expected Returns and Economics

### Staking Rewards
- **Block Rewards**: Earn CLORE for each block you stake
- **Transaction Fees**: Collect fees from transactions in your blocks
- **Frequency**: Depends on your stake size relative to network

### Factors Affecting Returns
- **Stake Amount**: Larger stakes = higher probability of rewards
- **Coin Age**: Older coins (up to maximum) have more weight
- **Network Weight**: Total network participation affects difficulty
- **Network Phase**: Different phases may have different reward structures

### Calculating Expected Returns
```bash
# Get basic staking metrics
YOUR_WEIGHT=$(clore-cli getstakinginfo | jq -r '.weight')
NETWORK_WEIGHT=$(clore-cli getposinfo | jq -r '.networkweight')
DIFFICULTY=$(clore-cli getposinfo | jq -r '.difficulty')

# Estimate time to next block (rough calculation)
BLOCK_TIME=60  # seconds
EXPECTED_SECONDS=$((NETWORK_WEIGHT * BLOCK_TIME / YOUR_WEIGHT))
EXPECTED_HOURS=$((EXPECTED_SECONDS / 3600))

echo "Estimated time to find next block: $EXPECTED_HOURS hours"
```

---

## Advanced Configuration

### Optimizing for Staking
```bash
# Add these to your clore.conf for better staking performance
cat >> ~/.clore/clore.conf << 'ADVANCED'

# Staking optimizations
stakesplitthreshold=2000
stakecombinethreshold=1000
maxorphantx=100
maxmempool=300

# Network optimizations
timeout=5000
maxreceivebuffer=5000
maxsendbuffer=1000

ADVANCED
```

### Running as a Service (Linux)
```bash
# Create systemd service file
sudo tee /etc/systemd/system/clore.service > /dev/null << 'SERVICE'
[Unit]
Description=CLORE Daemon
After=network.target

[Service]
Type=forking
User=clore
ExecStart=/usr/local/bin/clore_blockchaind -daemon -conf=/home/clore/.clore/clore.conf
ExecStop=/usr/local/bin/clore-cli stop
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
SERVICE

# Enable and start service
sudo systemctl enable clore
sudo systemctl start clore
```

---

## Getting Help and Support

### Community Resources
- **Discord**: Join the CLORE community Discord server
- **GitHub**: Report issues and contribute at github.com/clore-ai/clore
- **Documentation**: Visit docs.clore.ai for latest guides
- **Forums**: Community forums for discussions and support

### Debug Information
```bash
# Generate debug information for support requests
clore-cli getinfo > debug_info.txt
clore-cli getblockchaininfo >> debug_info.txt
clore-cli getstakinginfo >> debug_info.txt
clore-cli getwalletinfo >> debug_info.txt
```

---

## Conclusion

Full node staking with the command line client provides:
- **Maximum Security**: Complete control over your node and funds
- **Network Support**: Helps secure and decentralize the CLORE network
- **Full Features**: Access to all CLORE functionality
- **Higher Returns**: Potentially better returns due to direct participation

However, it requires:
- **Technical Knowledge**: Comfort with command line operations
- **Dedicated Resources**: Computer must run 24/7 for optimal staking
- **Active Monitoring**: Regular maintenance and monitoring required
- **Higher Responsibility**: Complete responsibility for security and backups

For users preferring simpler options with less technical overhead, see the [Light Client Staking Guide](staking_by_light_client.md) which uses cold staking delegation to authorized validators.

---

**⚠️ Important Disclaimer**: 
Cryptocurrency staking involves financial risk. Only stake funds you can afford to lose. This guide is for educational purposes and does not constitute financial advice. Always do your own research and consider consulting with financial professionals before making investment decisions.
