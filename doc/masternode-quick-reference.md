# CLORE Masternode Quick Reference

## Prerequisites Checklist

- [ ] 10,000 CLORE collateral ready
- [ ] VPS with static IP (Ubuntu 20.04+ recommended)
- [ ] Ports 8788/tcp and SSH open
- [ ] Authorized masternode status confirmed

## Installation Commands

### Server Setup

```bash
# System update and dependencies
apt update && apt upgrade -y
apt install -y curl wget unzip build-essential libtool autotools-dev \
    automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev \
    libdb4.8-dev libdb4.8++-dev libminiupnpc-dev libzmq3-dev

# User and firewall
adduser clore && usermod -aG sudo clore
ufw default deny incoming && ufw default allow outgoing
ufw allow ssh && ufw allow 8788/tcp && ufw --force enable
```

### CLORE Installation

```bash
# Download and install (replace version)
wget https://github.com/CloreBlockchain/clore/releases/download/vX.X.X/clore-X.X.X-x86_64-linux-gnu.tar.gz
tar -xzf clore-X.X.X-x86_64-linux-gnu.tar.gz
sudo cp clore-X.X.X/bin/* /usr/local/bin/
```

### Configuration File Template

```bash
# ~/.clore/clore.conf
cat > ~/.clore/clore.conf << EOF
rpcuser=rpcuser$(openssl rand -hex 8)
rpcpassword=$(openssl rand -hex 32)
rpcallowip=127.0.0.1
rpcport=8766
port=8788
masternode=1
externalip=YOUR_SERVER_IP
listen=1
server=1
daemon=1
maxconnections=256
debug=masternode
logips=1
logtimestamps=1
EOF
```

## Key Generation & Collateral

### Local Wallet Commands

```bash
# Generate masternode private key
createmasternodekey

# Find collateral after sending 10,000 CLORE
listmasternodeconf

# Check authorization status
listauthorizedmasternodes

# Start masternode
startmasternode alias false mn1
```

### Masternode.conf Format

```
alias server_ip:8788 masternode_private_key collateral_txid output_index
```

## Essential Monitoring Commands

### Server Status

```bash
# Daemon status
systemctl status clored
clore-cli getblockcount
clore-cli getconnectioncount

# Masternode status
clore-cli getmasternodestatus
clore-cli getmasternodeinfo

# Log monitoring
tail -f ~/.clore/debug.log
journalctl -f -u clored
```

### Expected Status Response

```json
{
  "txhash": "collateral_txid",
  "outputidx": 0,
  "netaddr": "server_ip:8788",
  "addr": "collateral_address",
  "status": 4,
  "message": "Masternode successfully started"
}
```

## Systemd Service

### Service File

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

sudo systemctl enable clored && sudo systemctl start clored
```

## Troubleshooting Commands

### Network Issues

```bash
# Test port connectivity
telnet server_ip 8788
netstat -tlnp | grep :8788

# Add seed nodes
clore-cli addnode "seed.clore.ai" add
clore-cli addnode "seed1.clore.ai" add

# Peer information
clore-cli getpeerinfo
```

### Common Fixes

```bash
# Restart daemon
clore-cli stop && clore_blockchaind

# Rebuild blockchain (last resort)
clore-cli stop
rm -rf ~/.clore/blocks ~/.clore/chainstate
clore_blockchaind
```

### Log Analysis

```bash
# Filter masternode messages
grep -i masternode ~/.clore/debug.log | tail -20

# Check for errors
grep -i error ~/.clore/debug.log | tail -10

# Monitor real-time
tail -f ~/.clore/debug.log | grep -i masternode
```

## Status Codes

- **Status 1**: Not capable (invalid configuration)
- **Status 2**: Remote activation (waiting for start command)
- **Status 3**: Input too new (collateral needs more confirmations)
- **Status 4**: Successfully started (operational)
- **Status 8**: Position in queue (waiting for activation)

## Network Specifications

- **Port**: 8788 (P2P)
- **RPC Port**: 8766 (local only)
- **Collateral**: 10,000 CLORE (exactly)
- **Confirmations**: 15 minimum
- **Block Time**: 60 seconds

## Emergency Recovery

### Server Migration

1. Stop old masternode: `clore-cli stop`
2. Setup new server with same private key
3. Update IP in local masternode.conf
4. Start from local wallet: `startmasternode alias false mn1`

### Key Recovery

- **Private Key**: From `masternode.conf` or backup
- **Collateral**: Never move the 10,000 CLORE transaction
- **Configuration**: Backup `~/.clore/clore.conf` regularly

## Maintenance Schedule

### Daily

- Check daemon status and block height
- Verify masternode status (status code 4)
- Monitor server resources

### Weekly

- System package updates
- Log file review
- Firewall status check

### Monthly

- CLORE software update check
- Performance metrics review
- Configuration backup

## Quick Links

- **Status Check**: `clore-cli getmasternodestatus`
- **Network Info**: `clore-cli getnetworkinfo`
- **Block Height**: `clore-cli getblockcount`
- **Authorization**: `clore-cli listauthorizedmasternodes`
- **Start MN**: `startmasternode alias false mn1` (local wallet)

---

_Quick Reference v1.0 - December 2024_
