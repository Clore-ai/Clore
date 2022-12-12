Sample init scripts and service configuration for clore_blockchaind
==========================================================

Sample scripts and configuration files for systemd, Upstart and OpenRC
can be found in the contrib/init folder.

    contrib/init/clore_blockchaind.service:    systemd service unit configuration
    contrib/init/clore_blockchaind.openrc:     OpenRC compatible SysV style init script
    contrib/init/clore_blockchaind.openrcconf: OpenRC conf.d file
    contrib/init/clore_blockchaind.conf:       Upstart service configuration file
    contrib/init/clore_blockchaind.init:       CentOS compatible SysV style init script

Service User
---------------------------------

All three Linux startup configurations assume the existence of a "clore" user
and group.  They must be created before attempting to use these scripts.
The OS X configuration assumes clore_blockchaind will be set up for the current user.

Configuration
---------------------------------

At a bare minimum, clore_blockchaind requires that the rpcpassword setting be set
when running as a daemon.  If the configuration file does not exist or this
setting is not set, clore_blockchaind will shutdown promptly after startup.

This password does not have to be remembered or typed as it is mostly used
as a fixed token that clore_blockchaind and client programs read from the configuration
file, however it is recommended that a strong and secure password be used
as this password is security critical to securing the wallet should the
wallet be enabled.

If clore_blockchaind is run with the "-server" flag (set by default), and no rpcpassword is set,
it will use a special cookie file for authentication. The cookie is generated with random
content when the daemon starts, and deleted when it exits. Read access to this file
controls who can access it through RPC.

By default the cookie is stored in the data directory, but it's location can be overridden
with the option '-rpccookiefile'.

This allows for running clore_blockchaind without having to do any manual configuration.

`conf`, `pid`, and `wallet` accept relative paths which are interpreted as
relative to the data directory. `wallet` *only* supports relative paths.

For an example configuration file that describes the configuration settings,
see `contrib/debian/examples/clore.conf`.

Paths
---------------------------------

### Linux

All three configurations assume several paths that might need to be adjusted.

Binary:              `/usr/bin/clore_blockchaind`  
Configuration file:  `/etc/clore/clore.conf`  
Data directory:      `/var/lib/clore_blockchaind`  
PID file:            `/var/run/clore_blockchaind/clore_blockchaind.pid` (OpenRC and Upstart) or `/var/lib/clore_blockchaind/clore_blockchaind.pid` (systemd)  
Lock file:           `/var/lock/subsys/clore_blockchaind` (CentOS)  

The configuration file, PID directory (if applicable) and data directory
should all be owned by the clore user and group.  It is advised for security
reasons to make the configuration file and data directory only readable by the
clore user and group.  Access to clore-cli and other clore_blockchaind rpc clients
can then be controlled by group membership.

### Mac OS X

Binary:              `/usr/local/bin/clore_blockchaind`  
Configuration file:  `~/Library/Application Support/Clore/clore.conf`  
Data directory:      `~/Library/Application Support/Clore`  
Lock file:           `~/Library/Application Support/Clore/.lock`  

Installing Service Configuration
-----------------------------------

### systemd

Installing this .service file consists of just copying it to
/usr/lib/systemd/system directory, followed by the command
`systemctl daemon-reload` in order to update running systemd configuration.

To test, run `systemctl start clore_blockchaind` and to enable for system startup run
`systemctl enable clore_blockchaind`

### OpenRC

Rename clore_blockchaind.openrc to clore_blockchaind and drop it in /etc/init.d.  Double
check ownership and permissions and make it executable.  Test it with
`/etc/init.d/clore_blockchaind start` and configure it to run on startup with
`rc-update add clore_blockchaind`

### Upstart (for Debian/Ubuntu based distributions)

Drop clore_blockchaind.conf in /etc/init.  Test by running `service clore_blockchaind start`
it will automatically start on reboot.

NOTE: This script is incompatible with CentOS 5 and Amazon Linux 2014 as they
use old versions of Upstart and do not supply the start-stop-daemon utility.

### CentOS

Copy clore_blockchaind.init to /etc/init.d/clore_blockchaind. Test by running `service clore_blockchaind start`.

Using this script, you can adjust the path and flags to the clore_blockchaind program by
setting the CLORE_BLOCKCHAIND and FLAGS environment variables in the file
/etc/sysconfig/clore_blockchaind. You can also use the DAEMONOPTS environment variable here.

### Mac OS X

Copy org.clore.clore_blockchaind.plist into ~/Library/LaunchAgents. Load the launch agent by
running `launchctl load ~/Library/LaunchAgents/org.clore.clore_blockchaind.plist`.

This Launch Agent will cause clore_blockchaind to start whenever the user logs in.

NOTE: This approach is intended for those wanting to run clore_blockchaind as the current user.
You will need to modify org.clore.clore_blockchaind.plist if you intend to use it as a
Launch Daemon with a dedicated clore user.

Auto-respawn
-----------------------------------

Auto respawning is currently only configured for Upstart and systemd.
Reasonable defaults have been chosen but YMMV.
