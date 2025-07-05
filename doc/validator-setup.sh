#!/bin/bash

# CLORE Validator Automated Setup Script
# Compatible with Ubuntu 20.04+ and Debian-based systems
# Version: 1.0

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Script information
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SETUP_LOG="$SCRIPT_DIR/validator-setup.log"
CONFIG_INFO="$SCRIPT_DIR/validator-info.txt"
SERVICE_USER="clore"
CLORE_DIR="/home/$SERVICE_USER/.clore"
REPO_URL="https://github.com/CloreBlockchain/clore.git"
BUILD_DIR="/tmp/clore-build"

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [INFO] $1" >> "$SETUP_LOG"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [WARNING] $1" >> "$SETUP_LOG"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [ERROR] $1" >> "$SETUP_LOG"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [SUCCESS] $1" >> "$SETUP_LOG"
}

print_header() {
    echo -e "${PURPLE}=====================================${NC}"
    echo -e "${PURPLE}$1${NC}"
    echo -e "${PURPLE}=====================================${NC}"
}

# Function to check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        print_error "This script should not be run as root for security reasons."
        print_error "Please run as a regular user with sudo privileges."
        exit 1
    fi
}

# Function to check system requirements
check_system() {
    print_header "CHECKING SYSTEM REQUIREMENTS"
    
    # Check OS
    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        print_status "Operating System: $NAME $VERSION"
    else
        print_error "Cannot determine operating system"
        exit 1
    fi
    
    # Check architecture
    ARCH=$(uname -m)
    print_status "Architecture: $ARCH"
    if [[ "$ARCH" != "x86_64" ]]; then
        print_warning "This script is optimized for x86_64 architecture"
    fi
    
    # Check available memory
    TOTAL_RAM=$(free -m | awk 'NR==2{print $2}')
    print_status "Total RAM: ${TOTAL_RAM}MB"
    if [[ $TOTAL_RAM -lt 3500 ]]; then
        print_warning "Less than 4GB RAM detected. Validator may experience performance issues."
    fi
    
    # Check available disk space
    AVAILABLE_SPACE=$(df / | awk 'NR==2{print $4}')
    AVAILABLE_SPACE_GB=$((AVAILABLE_SPACE / 1024 / 1024))
    print_status "Available disk space: ${AVAILABLE_SPACE_GB}GB"
    if [[ $AVAILABLE_SPACE_GB -lt 50 ]]; then
        print_warning "Less than 50GB free space detected. Consider adding more storage."
    fi
    
    # Check sudo access
    if sudo -n true 2>/dev/null; then
        print_status "Sudo access confirmed"
    else
        print_error "This script requires sudo access. Please run 'sudo -v' first."
        exit 1
    fi
}

# Function to install dependencies
install_dependencies() {
    print_header "INSTALLING DEPENDENCIES"
    
    print_status "Updating package repositories..."
    sudo apt update -y
    
    print_status "Installing build dependencies..."
    sudo apt install -y \
        build-essential \
        git \
        autoconf \
        automake \
        libtool \
        pkg-config \
        libssl-dev \
        libevent-dev \
        bsdmainutils \
        libboost-all-dev \
        libdb4.8-dev \
        libdb4.8++-dev \
        libminiupnpc-dev \
        libzmq3-dev \
        curl \
        wget \
        unzip \
        ufw \
        htop \
        nano
    
    print_success "Dependencies installed successfully"
}

# Function to create system user
create_user() {
    print_header "CREATING SYSTEM USER"
    
    if id "$SERVICE_USER" &>/dev/null; then
        print_status "User '$SERVICE_USER' already exists"
    else
        print_status "Creating user '$SERVICE_USER'..."
        sudo adduser --disabled-password --gecos "" "$SERVICE_USER"
        sudo usermod -aG sudo "$SERVICE_USER"
        print_success "User '$SERVICE_USER' created successfully"
    fi
}

# Function to configure firewall
configure_firewall() {
    print_header "CONFIGURING FIREWALL"
    
    print_status "Configuring UFW firewall..."
    
    # Reset UFW to defaults
    sudo ufw --force reset
    
    # Set default policies
    sudo ufw default deny incoming
    sudo ufw default allow outgoing
    
    # Allow SSH (detect current SSH port)
    SSH_PORT=$(sudo ss -tlnp | grep sshd | awk '{print $4}' | cut -d':' -f2 | head -1)
    if [[ -n "$SSH_PORT" ]]; then
        sudo ufw allow "$SSH_PORT"/tcp comment 'SSH'
        print_status "Allowed SSH on port $SSH_PORT"
    else
        sudo ufw allow ssh
        print_status "Allowed SSH on default port"
    fi
    
    # Allow CLORE P2P port
    sudo ufw allow 8788/tcp comment 'CLORE P2P'
    print_status "Allowed CLORE P2P port 8788"
    
    # Enable firewall
    sudo ufw --force enable
    
    print_success "Firewall configured successfully"
    sudo ufw status numbered
}

# Function to clone and build CLORE
build_clore() {
    print_header "BUILDING CLORE FROM SOURCE"
    
    # Clean up any existing build directory
    if [[ -d "$BUILD_DIR" ]]; then
        print_status "Removing existing build directory..."
        rm -rf "$BUILD_DIR"
    fi
    
    print_status "Cloning CLORE repository..."
    git clone "$REPO_URL" "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Show current commit
    CURRENT_COMMIT=$(git rev-parse --short HEAD)
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
    print_status "Building from branch: $CURRENT_BRANCH (commit: $CURRENT_COMMIT)"
    
    print_status "Generating build configuration..."
    ./autogen.sh
    
    print_status "Configuring build..."
    ./configure --disable-tests --disable-bench --without-gui --disable-man
    
    print_status "Compiling CLORE (this may take 10-30 minutes)..."
    make -j$(nproc)
    
    print_status "Installing CLORE binaries..."
    sudo make install
    
    # Verify installation
    if command -v clore_blockchaind &> /dev/null; then
        CLORE_VERSION=$(clore_blockchaind --version | head -1)
        print_success "CLORE installed successfully: $CLORE_VERSION"
    else
        print_error "CLORE installation failed"
        exit 1
    fi
    
    # Clean up build directory
    cd /
    rm -rf "$BUILD_DIR"
    print_status "Build directory cleaned up"
}

# Function to generate configuration
generate_config() {
    print_header "GENERATING VALIDATOR CONFIGURATION"
    
    # Generate secure random credentials
    RPC_USER="rpcuser$(openssl rand -hex 4)"
    RPC_PASSWORD=$(openssl rand -hex 32)
    
    # Get external IP
    print_status "Detecting external IP address..."
    EXTERNAL_IP=$(curl -s https://ipinfo.io/ip || curl -s https://icanhazip.com || curl -s https://ident.me)
    if [[ -z "$EXTERNAL_IP" ]]; then
        print_warning "Could not detect external IP automatically"
        EXTERNAL_IP="YOUR_VPS_IP_HERE"
    else
        print_status "External IP detected: $EXTERNAL_IP"
    fi
    
    # Create CLORE directory
    print_status "Creating CLORE data directory..."
    sudo mkdir -p "$CLORE_DIR"
    sudo chown "$SERVICE_USER:$SERVICE_USER" "$CLORE_DIR"
    
    # Generate configuration file
    print_status "Generating clore.conf..."
    
    cat << EOF | sudo -u "$SERVICE_USER" tee "$CLORE_DIR/clore.conf" > /dev/null
# CLORE Validator Configuration
# Generated on $(date)

# RPC Configuration
rpcuser=$RPC_USER
rpcpassword=$RPC_PASSWORD
rpcallowip=127.0.0.1
rpcport=8766

# Network Configuration
port=8788
listen=1
server=1
daemon=1
maxconnections=256

# Validator Configuration
validator=1
externalip=$EXTERNAL_IP

# Logging Configuration
debug=validator
logips=1
logtimestamps=1
shrinkdebugfile=1

# Performance Settings
dbcache=256
maxorphantx=10
maxmempool=50
mempoolexpiry=24

# Network optimizations
timeout=5000
EOF
    
    sudo chown "$SERVICE_USER:$SERVICE_USER" "$CLORE_DIR/clore.conf"
    sudo chmod 600 "$CLORE_DIR/clore.conf"
    
    print_success "Configuration file created"
}

# Function to create systemd service
create_systemd_service() {
    print_header "CREATING SYSTEMD SERVICE"
    
    print_status "Creating systemd service file..."
    
    cat << EOF | sudo tee /etc/systemd/system/clore_blockchaind.service > /dev/null
[Unit]
Description=CLORE Blockchain Daemon
Documentation=https://github.com/CloreBlockchain/clore
After=network.target
Wants=network.target

[Service]
Type=forking
User=$SERVICE_USER
Group=$SERVICE_USER
WorkingDirectory=/home/$SERVICE_USER
ExecStart=/usr/local/bin/clore_blockchaind -conf=/home/$SERVICE_USER/.clore/clore.conf -datadir=/home/$SERVICE_USER/.clore
ExecStop=/usr/local/bin/clore-cli -conf=/home/$SERVICE_USER/.clore/clore.conf -datadir=/home/$SERVICE_USER/.clore stop
Restart=always
RestartSec=10
TimeoutStartSec=60
TimeoutStopSec=60
KillMode=mixed

# Security settings
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=full
ProtectHome=true

# Resource limits
LimitNOFILE=8192

[Install]
WantedBy=multi-user.target
EOF
    
    print_status "Reloading systemd daemon..."
    sudo systemctl daemon-reload
    
    print_status "Enabling clore_blockchaind service..."
    sudo systemctl enable clore_blockchaind
    
    print_success "Systemd service created and enabled"
}

# Function to save setup information
save_setup_info() {
    print_header "SAVING SETUP INFORMATION"
    
    cat << EOF > "$CONFIG_INFO"
# CLORE Validator Setup Information
# Generated on $(date)
# Host: $(hostname)
# External IP: $EXTERNAL_IP

## IMPORTANT CREDENTIALS
RPC Username: $RPC_USER
RPC Password: $RPC_PASSWORD

## CONFIGURATION FILES
CLORE Config: $CLORE_DIR/clore.conf
Service File: /etc/systemd/system/clore_blockchaind.service
Log File: $CLORE_DIR/debug.log

## NETWORK INFORMATION
External IP: $EXTERNAL_IP
P2P Port: 8788
RPC Port: 8766 (localhost only)

## NEXT STEPS REQUIRED:
1. Get your validator private key from your LOCAL wallet:
   - Open CLORE wallet console
   - Run: createvalidatorkey
   - Copy the generated key

2. Edit the configuration file:
   sudo nano $CLORE_DIR/clore.conf
   - Replace "REPLACE_WITH_YOUR_VALIDATOR_PRIVATE_KEY" with your actual key

3. Prepare collateral (on your LOCAL wallet):
   - Send exactly 10,000 CLORE to a new address
   - Wait for 15 confirmations
   - Run: listvalidatorconf
   - Note transaction ID and output index

4. Add validator entry to your LOCAL validator.conf:
   mn1 $EXTERNAL_IP:8788 YOUR_VALIDATOR_PRIVATE_KEY TX_ID OUTPUT_INDEX

5. Start the validator:
   - Start daemon: sudo systemctl start clore_blockchaind
   - Wait for sync: clore-cli getblockcount
   - Start from local wallet: startvalidator alias false mn1
   - Check status: clore-cli getvalidatorstatus

## USEFUL COMMANDS
Check service status: sudo systemctl status clore_blockchaind
View logs: sudo journalctl -f -u clore_blockchaind
Check block height: clore-cli getblockcount
Check connections: clore-cli getconnectioncount
Check validator status: clore-cli getvalidatorstatus

## FIREWALL STATUS
$(sudo ufw status numbered)

## BUILD INFORMATION
CLORE Version: $(clore_blockchaind --version | head -1)
Build Date: $(date)
Build User: $(whoami)
Build Host: $(hostname)
EOF
    
    # Set appropriate permissions
    chmod 600 "$CONFIG_INFO"
    
    print_success "Setup information saved to: $CONFIG_INFO"
}

# Function to display final summary
display_summary() {
    print_header "SETUP COMPLETE!"
    
    echo -e "${GREEN}"
    echo "🎉 CLORE Validator setup completed successfully!"
    echo -e "${NC}"
    
    echo -e "${CYAN}📋 SUMMARY:${NC}"
    echo "• CLORE daemon built and installed"
    echo "• System user '$SERVICE_USER' created"
    echo "• Firewall configured (ports 8788, SSH)"
    echo "• Systemd service 'clore_blockchaind' created and enabled"
    echo "• Configuration files generated"
    
    echo -e "${YELLOW}"
    echo "⚠️  IMPORTANT NEXT STEPS:"
    echo "1. Get validator private key from your LOCAL wallet"
    echo "2. Edit config: sudo nano $CLORE_DIR/clore.conf"
    echo "3. Replace the validator private key placeholder"
    echo "4. Prepare 10,000 CLORE collateral"
    echo "5. Start the service: sudo systemctl start clore_blockchaind"
    echo -e "${NC}"
    
    echo -e "${PURPLE}📁 IMPORTANT FILES:${NC}"
    echo "• Setup info: $CONFIG_INFO"
    echo "• Setup log: $SETUP_LOG"
    echo "• Config file: $CLORE_DIR/clore.conf"
    
    echo -e "${BLUE}🔧 USEFUL COMMANDS:${NC}"
    echo "• Start service: sudo systemctl start clore_blockchaind"
    echo "• Check status: sudo systemctl status clore_blockchaind"
    echo "• View logs: sudo journalctl -f -u clore_blockchaind"
    echo "• Check sync: clore-cli getblockcount"
    echo "• Validator status: clore-cli getvalidatorstatus"
    
    echo -e "${GREEN}"
    echo "📖 Full documentation available in: $SCRIPT_DIR/validator-simple.md"
    echo "💬 Support: CLORE Discord #validator-support"
    echo -e "${NC}"
}

# Function to handle script interruption
cleanup() {
    print_warning "Script interrupted. Cleaning up..."
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
    fi
    exit 130
}

# Main execution function
main() {
    # Set up signal handlers
    trap cleanup SIGINT SIGTERM
    
    # Initialize log file
    echo "CLORE Validator Setup Log - $(date)" > "$SETUP_LOG"
    
    print_header "CLORE VALIDATOR AUTOMATED SETUP"
    print_status "Starting automated validator setup..."
    print_status "Script directory: $SCRIPT_DIR"
    print_status "Log file: $SETUP_LOG"
    
    # Run setup steps
    check_root
    check_system
    install_dependencies
    create_user
    configure_firewall
    build_clore
    generate_config
    create_systemd_service
    save_setup_info
    display_summary
    
    print_success "Automated setup completed successfully!"
    print_status "Total setup time: $SECONDS seconds"
}

# Script entry point
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi 