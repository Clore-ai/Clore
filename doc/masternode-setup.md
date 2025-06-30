# CLORE Masternode Setup Guide

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Server Preparation](#server-preparation)
4. [CLORE Daemon Installation](#clore-daemon-installation)
5. [Wallet Setup and Collateral](#wallet-setup-and-collateral)
6. [Masternode Configuration](#masternode-configuration)
7. [Starting Your Masternode](#starting-your-masternode)
8. [Monitoring and Maintenance](#monitoring-and-maintenance)
9. [Troubleshooting](#troubleshooting)
10. [Security Best Practices](#security-best-practices)

---

## Overview

This guide provides comprehensive instructions for setting up and operating a CLORE masternode. A masternode is a specially configured node that provides enhanced services to the CLORE network and receives rewards for its participation.

**Important:** CLORE masternodes require authorization during the current network phase. Ensure you are on the authorized masternode list before proceeding.

### Masternode Requirements

- **Collateral**: 10,000 CLORE (must remain unspent)
- **VPS**: Dedicated server with static IP address
- **Network**: 24/7 uptime with stable internet connection
- **Authorization**: Must be on the authorized masternode list

---

## Prerequisites

### System Requirements

- **Operating System**: Ubuntu 22.04 LTS or newer (recommended)
- **RAM**: Minimum 8GB
- **Storage**: Minimum 200GB SSD
- **CPU**: 4+ cores
- **Bandwidth**: Unlimited or high (1TB) monthly allowance
- **Network**: Static IP address with port 8788 open

### Required Knowledge

- Basic Linux command line operations
- Understanding of cryptocurrency wallets and transactions
- Basic networking concepts (ports, firewalls)
- Text editor usage (nano, vim, etc.)

### Required Software

- SSH client Putty or (Termius for Windows, macOS, Linux)
- CLORE wallet with 10,000+ CLORE for collateral

---

## Server Preparation

### 1. Initial Server Setup

Connect to your server via SSH:

```bash
ssh root@YOUR_SERVER_IP
```

Update the system:

```bash
apt update && apt upgrade -y
```

Install required dependencies:

```bash
apt install -y curl wget unzip build-essential libtool autotools-dev \
    automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev \
    libdb4.8-dev libdb4.8++-dev libminiupnpc-dev libzmq3-dev
```

### 2. Create System User

Create a dedicated user for the masternode:

```bash
adduser clore
usermod -aG sudo clore
```

Switch to the new user:

```bash
su - clore
```

### 3. Configure Firewall

Configure UFW (if not already configured):

```bash
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow ssh
sudo ufw allow 8788/tcp
sudo ufw --force enable
```

Verify firewall status:

```bash
sudo ufw status
```

---

## CLORE Daemon Installation

### 1. Download CLORE Core

Navigate to the home directory:

```bash
cd ~
```

Download the latest CLORE release (replace with actual version):

```bash
wget https://github.com/CloreBlockchain/clore/releases/download/vX.X.X/clore-X.X.X-x86_64-linux-gnu.tar.gz
```

Extract the archive:

```bash
tar -xzf clore-X.X.X-x86_64-linux-gnu.tar.gz
```

Move binaries to system path:

```bash
sudo cp clore-X.X.X/bin/* /usr/local/bin/
```

Verify installation:

```bash
clore_blockchaind --version
```

### 2. Create Configuration Directory

Create the CLORE data directory:

```bash
mkdir -p ~/.clore
```

### 3. Initial Configuration

Create the initial configuration file:

```bash
cat > ~/.clore/clore.conf << EOF
# Basic Configuration
rpcuser=rpcuser$(openssl rand -hex 8)
rpcpassword=$(openssl rand -hex 32)
rpcallowip=127.0.0.1
rpcport=8766
port=8788

# Masternode Configuration
masternode=1
externalip=YOUR_SERVER_IP

# Network Configuration
listen=1
server=1
daemon=1
maxconnections=256

# Logging
debug=masternode
logips=1
logtimestamps=1
EOF
```

**Note**: Replace `MASTERNODE_PRIVATE_KEY_HERE` and `YOUR_SERVER_IP` with actual values (obtained in subsequent steps).

---

## Wallet Setup and Collateral

### 1. Generate Masternode Private Key

TODO:

```
createmasternodekey
```

Save this key securely - you'll need it for the server configuration.

### 2. Prepare Collateral Transaction

In your local wallet:

1. Send exactly 10,000 CLORE to a new address in your wallet
2. Wait for 15 confirmations
3. In the debug console, find your collateral transaction:
   ```
   listmasternodeconf
   ```

Note the transaction ID (txhash) and output index.

### 3. Update Server Configuration

On your server, edit the configuration file:

```bash
nano ~/.clore/clore.conf
```

Update these lines:

- Replace `MASTERNODE_PRIVATE_KEY_HERE` with the key from step 1
- Replace `YOUR_SERVER_IP` with your server's IP address

---

## Masternode Configuration

### 1. Create Masternode Configuration Entry

On your LOCAL wallet, create or edit `masternode.conf`:

**Location:**

- **Windows**: `%APPDATA%\CLORE\masternode.conf`
- **macOS**: `~/Library/Application Support/CLORE/masternode.conf`
- **Linux**: `~/.clore/masternode.conf`

Add this line (replace with your values):

```
mn1 YOUR_SERVER_IP:8788 MASTERNODE_PRIVATE_KEY COLLATERAL_TXID COLLATERAL_OUTPUT_INDEX
```

**Example:**

```
mn1 192.168.1.100:8788 7VatqRx...privatekey...8xNc4D 15a94b...txhash...7c3f 0
```

### 2. Verify Authorization

Check if your address is authorized:

```bash
clore-cli listauthorizedmasternodes
```

If you're not on the list, contact the CLORE team for authorization.

---

## Starting Your Masternode

### 1. Start the CLORE Daemon

On your server, start the daemon:

```bash
clore_blockchaind
```

Wait for the blockchain to sync:

```bash
clore-cli getblockcount
```

Compare with the current block height from a block explorer.

### 2. Start Masternode from Local Wallet

In your local wallet's debug console:

```
startmasternode alias false mn1
```

Or start all masternodes:

```
startmasternode all false
```

### 3. Verify Masternode Status

On your server, check the masternode status:

```bash
clore-cli getmasternodestatus
```

You should see:

```json
{
  "txhash": "your_collateral_txid",
  "outputidx": 0,
  "netaddr": "your_server_ip:8788",
  "addr": "your_collateral_address",
  "status": 4,
  "message": "Masternode successfully started"
}
```

---

## Monitoring and Maintenance

### 1. System Service Setup

Create a systemd service for automatic startup:

```bash
sudo tee /etc/systemd/system/clored.service << EOF
[Unit]
Description=CLORE Daemon
After=network.target

[Service]
Type=forking
User=clore
Group=clore
WorkingDirectory=/home/clore
ExecStart=/usr/local/bin/clore_blockchaind
ExecStop=/usr/local/bin/clore-cli stop
Restart=always
RestartSec=10
KillMode=mixed
TimeoutStopSec=60

[Install]
WantedBy=multi-user.target
EOF
```

Enable and start the service:

```bash
sudo systemctl enable clored
sudo systemctl start clored
```

### 2. Monitoring Commands

Check daemon status:

```bash
sudo systemctl status clored
```

View recent logs:

```bash
tail -f ~/.clore/debug.log
```

Check masternode info:

```bash
clore-cli getmasternodeinfo
```

Monitor network connection:

```bash
clore-cli getconnectioncount
```

### 3. Regular Maintenance

**Daily Checks:**

- Verify daemon is running: `clore-cli getblockcount`
- Check masternode status: `clore-cli getmasternodestatus`
- Monitor server resources: `htop` or `free -h`

**Weekly Tasks:**

- Update system packages: `sudo apt update && sudo apt upgrade`
- Review log files for errors
- Verify firewall configuration

**Monthly Tasks:**

- Check for CLORE software updates
- Review server performance metrics
- Backup configuration files

---

## Troubleshooting

### Common Issues

**1. Masternode Not Starting**

```bash
# Check configuration file
cat ~/.clore/clore.conf

# Verify private key format
clore-cli validateaddress $(clore-cli getaccountaddress "")

# Check if ports are accessible
netstat -tlnp | grep :8788
```

**2. Blockchain Not Syncing**

```bash
# Check peer connections
clore-cli getconnectioncount

# Add nodes manually
clore-cli addnode "seed.clore.ai" add
clore-cli addnode "seed1.clore.ai" add

# Restart daemon if necessary
clore-cli stop
clore_blockchaind
```

**3. "Not in the masternode list" Error**

- Verify you're on the authorized masternode list
- Ensure collateral transaction has enough confirmations (15+)
- Check that collateral amount is exactly 10,000 CLORE

**4. Connection Issues**

```bash
# Test external connectivity
telnet YOUR_SERVER_IP 8788

# Check firewall rules
sudo ufw status numbered

# Verify UFW is allowing the port
sudo ufw allow 8788/tcp
```

### Log Analysis

Common log locations:

```bash
# Main debug log
tail -f ~/.clore/debug.log

# System service logs
sudo journalctl -f -u clored

# Filter for masternode messages
grep -i masternode ~/.clore/debug.log | tail -20
```

### Recovery Procedures

**If your masternode goes offline:**

1. Check server connectivity and daemon status
2. Restart the CLORE daemon if necessary
3. Verify configuration hasn't changed
4. Re-start masternode from local wallet if needed

**If you need to change servers:**

1. Stop the old masternode gracefully
2. Set up the new server following this guide
3. Use the same masternode private key and collateral
4. Update the IP address in your local masternode.conf
5. Start the masternode from your local wallet

---

## Security Best Practices

### Server Security

1. **SSH Key Authentication**

   ```bash
   # Disable password authentication
   sudo sed -i 's/#PasswordAuthentication yes/PasswordAuthentication no/' /etc/ssh/sshd_config
   sudo systemctl restart ssh
   ```

2. **Regular Security Updates**

   ```bash
   # Enable automatic security updates
   sudo apt install unattended-upgrades
   sudo dpkg-reconfigure -plow unattended-upgrades
   ```

3. **Fail2Ban Protection**
   ```bash
   sudo apt install fail2ban
   sudo systemctl enable fail2ban
   sudo systemctl start fail2ban
   ```

### Wallet Security

1. **Backup Your Wallet**

   - Keep encrypted backups of your wallet.dat file
   - Store masternode private keys securely offline
   - Document your masternode configuration

2. **Collateral Security**

   - Never move or spend your 10,000 CLORE collateral
   - Use a separate wallet or address for daily transactions
   - Consider using a hardware wallet for collateral storage

3. **Access Control**
   - Use strong, unique passwords for all accounts
   - Enable two-factor authentication where possible
   - Limit server access to necessary personnel only

### Network Security

1. **VPN Considerations**

   - Consider using a VPN for administrative access
   - Ensure VPN doesn't interfere with masternode connectivity

2. **DDoS Protection**

   - Choose hosting providers with DDoS protection
   - Consider using a CDN service for additional protection

3. **Monitoring**
   - Set up uptime monitoring for your masternode
   - Configure alerts for unusual network activity

---

## Additional Resources

- **CLORE Official Website**: https://clore.ai
- **CLORE GitHub Repository**: https://github.com/CloreBlockchain/clore
- **CLORE Discord Community**: [Join the community for support]
- **Block Explorer**: [Link to official block explorer]

### Support Channels

If you encounter issues not covered in this guide:

1. Check the CLORE Discord community #masternode-support channel
2. Review GitHub issues for known problems
3. Consult the CLORE documentation repository

---

## Conclusion

Successfully setting up a CLORE masternode requires careful attention to security, configuration, and ongoing maintenance. This guide provides the foundation for a secure and reliable masternode operation.

Remember that masternode operators play a crucial role in the CLORE network's security and functionality. Maintain high uptime, keep your software updated, and follow security best practices to ensure optimal performance.

**Important**: Always test configuration changes on a non-production environment when possible, and maintain current backups of your wallet and configuration files.

---

_Last Updated: December 2024_  
_Guide Version: 1.0_  
_Compatible with: CLORE Core v2.0+_
