 #!/usr/bin/env bash

 # Execute this file to install the clore cli tools into your path on OS X

 CURRENT_LOC="$( cd "$(dirname "$0")" ; pwd -P )"
 LOCATION=${CURRENT_LOC%Clore-Qt.app*}

 # Ensure that the directory to symlink to exists
 sudo mkdir -p /usr/local/bin

 # Create symlinks to the cli tools
 sudo ln -s ${LOCATION}/Clore-Qt.app/Contents/MacOS/clore_blockchaind /usr/local/bin/clore_blockchaind
 sudo ln -s ${LOCATION}/Clore-Qt.app/Contents/MacOS/clore-cli /usr/local/bin/clore-cli
