# CLORE Validator Automated Setup

## Quick Start

The automated setup script handles everything from building CLORE to configuring services. Perfect for users who want a one-command setup experience.

### Prerequisites

- Ubuntu 20.04+ or Debian-based Linux VPS
- Sudo access
- At least 4GB RAM and 50GB storage
- Static IP address

### Usage

1. **Download the script** to your VPS:

   ```bash
   wget https://raw.githubusercontent.com/CloreBlockchain/clore/main/doc/validator-setup.sh
   chmod +x validator-setup.sh
   ```

2. **Run the automated setup**:

   ```bash
   ./validator-setup.sh
   ```

3. **Follow the on-screen instructions** when the script completes

### What The Script Does

✅ **System Preparation**

- Checks system requirements (RAM, disk space, OS)
- Updates package repositories
- Installs all build dependencies

✅ **CLORE Installation**

- Clones latest CLORE source code
- Builds binaries from source (optimized build)
- Installs to system paths

✅ **Security Configuration**

- Creates dedicated `clore` user
- Configures UFW firewall (allows SSH + port 8788)
- Sets appropriate file permissions

✅ **Service Setup**

- Creates systemd service for automatic startup
- Generates secure RPC credentials
- Creates optimized configuration file

✅ **Documentation**

- Saves all credentials and information
- Creates detailed next-steps guide
- Logs entire setup process

### Generated Files

After successful completion, you'll find:

- **`validator-info.txt`** - All your credentials, next steps, and important information
- **`validator-setup.log`** - Complete setup log for troubleshooting
- **`/home/clore/.clore/clore.conf`** - Generated configuration file
- **`/etc/systemd/system/clored.service`** - Systemd service file

### Next Steps After Script Completion

The script prepares everything but **you still need to**:

1. **Get validator private key** from your LOCAL wallet:

   ```
   createvalidatorkey
   ```

2. **Edit the configuration**:

   ```bash
   sudo nano /home/clore/.clore/clore.conf
   # Replace: REPLACE_WITH_YOUR_VALIDATOR_PRIVATE_KEY
   ```

3. **Prepare collateral** (LOCAL wallet):

   - Send exactly 10,000 CLORE to new address
   - Wait 15 confirmations
   - Run: `listvalidatorconf`

4. **Start the validator**:
   ```bash
   sudo systemctl start clored
   clore-cli getblockcount  # wait for sync
   # Then from LOCAL wallet: startvalidator alias false mn1
   ```

### Useful Commands

```bash
# Service management
sudo systemctl start clored
sudo systemctl stop clored
sudo systemctl status clored
sudo systemctl restart clored

# Monitoring
sudo journalctl -f -u clored           # Live logs
clore-cli getblockcount               # Current block height
clore-cli getconnectioncount          # Peer connections
clore-cli getvalidatorstatus         # Validator status

# Firewall
sudo ufw status                       # Check firewall rules
```

### Troubleshooting

**Script fails during build?**

- Ensure you have at least 4GB RAM
- Check internet connection
- Re-run the script (it will skip completed steps)

**Can't connect after setup?**

- Check firewall: `sudo ufw status`
- Verify service: `sudo systemctl status clored`
- Check logs: `sudo journalctl -u clored`

**Validator not starting?**

- Verify you edited the config file with your private key
- Ensure collateral has 15+ confirmations
- Check if you're on the authorized validator list

### Manual vs Automated Setup

| Task               | Manual Setup | Automated Script |
| ------------------ | ------------ | ---------------- |
| Time Required      | 2-3 hours    | 30-45 minutes    |
| Error Prone        | High         | Low              |
| Documentation      | Manual       | Auto-generated   |
| Security Config    | Manual       | Automatic        |
| Build Optimization | Manual       | Optimized        |
| Service Creation   | Manual       | Automatic        |

### Support

- **Full Documentation**: See `validator-simple.md` for manual setup
- **Quick Reference**: See `validator-quick-reference.md` for commands
- **Community Support**: CLORE Discord #validator-support
- **Script Issues**: Check `validator-setup.log` for details

---

**🚀 One command. Full setup. Ready to go!**

_Automated Setup Guide v1.0 - December 2024_
