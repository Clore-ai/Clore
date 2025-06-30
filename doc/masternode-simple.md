# CLORE Masternode Simple Setup Guide

## Quick Overview

Setting up a CLORE masternode requires:

1. A VPS with Ubuntu and static IP
2. 10,000 CLORE collateral
3. Authorization from CLORE team

**Time Required:** 30-45 minutes

---

## Step 1: VPS Setup

### Get a VPS

- **Recommended**: Any Ubuntu 22.04 VPS with 4GB RAM
- **Examples**: DigitalOcean ($20/month), Vultr, Linode, AWS
- **Requirements**: Static IP address

### Basic VPS Setup

```bash
# Connect via SSH
ssh root@YOUR_VPS_IP

# Update system
apt update && apt upgrade -y

# Install dependencies
apt install -y wget curl unzip

# Create user
adduser clore
usermod -aG sudo clore
su - clore
```

---

## Step 2: Install CLORE

### Download and Install

```bash
# Download CLORE (replace with latest version)
wget https://github.com/CloreBlockchain/clore/releases/download/vX.X.X/clore-X.X.X-x86_64-linux-gnu.tar.gz

# Extract and install
tar -xzf clore-*.tar.gz
sudo cp clore-*/bin/* /usr/local/bin/

# Create config directory
mkdir ~/.clore
```

---

## Step 3: Configuration

### Create Basic Config

```bash
nano ~/.clore/clore.conf
```

Add this content (replace the highlighted parts):

```
rpcuser=admin
rpcpassword=your_random_password_here
rpcport=8766
port=8788
masternode=1
externalip=YOUR_VPS_IP_ADDRESS
listen=1
server=1
daemon=1
```

### Get Your Masternode Private Key

On your **local CLORE wallet** (not the VPS), open Console and run:

```
createmasternodekey
```

Copy this key to replace `YOUR_MASTERNODE_PRIVATE_KEY` above.

---

## Step 4: Collateral Setup

### Prepare Collateral (Local Wallet)

1. Send exactly **10,000 CLORE** to a new address in your wallet
2. Wait for 15 confirmations
3. In Console, run: `listmasternodeconf`
4. Note the transaction ID and output index

### Create Masternode Entry (Local Wallet)

Add this line to your local `masternode.conf` file:

```
mn1 YOUR_VPS_IP:8788 YOUR_MASTERNODE_PRIVATE_KEY YOUR_TX_ID YOUR_OUTPUT_INDEX
```

**Example:**

```
mn1 192.168.1.100:8788 7VatqRx...privatekey...8xNc4D 15a94b...txhash...7c3f 0
```

---

## Step 5: Start Everything

### Start VPS Daemon

```bash
# On your VPS
clore_blockchaind

# Wait for sync (check with)
clore-cli getblockcount
```

### Start Masternode (Local Wallet)

In your local wallet Console:

```
startmasternode alias false mn1
```

### Check Status (VPS)

```bash
clore-cli getmasternodestatus
```

You should see `"status": 4` and `"message": "Masternode successfully started"`

---

## That's It!

Your masternode should now be running. Check status periodically with:

```bash
clore-cli getmasternodestatus
```

## Quick Troubleshooting

**Not syncing?** Check connections:

```bash
clore-cli getconnectioncount
```

**Status not 4?** Wait a few minutes and check again. Sometimes it takes time.

**Still having issues?** Ask in CLORE Discord #masternode-support

---

_Simple Setup Guide - 5 steps, 30 minutes, done!_
