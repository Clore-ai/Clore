// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2021 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_MASTERNODEMAN_H
#define CLORE_MASTERNODEMAN_H

#include "activemasternode.h"
#include "cyclingvector.h"
#include "key.h"
#include "key_io.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "util/system.h"

#define MASTERNODES_REQUEST_SECONDS (60 * 60) // One hour.

/** Maximum number of block hashes to cache */
static const unsigned int CACHED_BLOCK_HASHES = 200;

// TODO: Add PIVX masternode manager class forward declarations
/*
class CMasternodeMan;
class CActiveMasternode;

extern CMasternodeMan mnodeman;
extern CActiveMasternode activeMasternode;

void DumpMasternodes();
*/

/** Access to the MN database (mncache.dat)
 */
// TODO: Add PIVX CMasternodeDB class
/*
class CMasternodeDB
{
    // ... PIVX implementation ...
};
*/

// TODO: Add PIVX CMasternodeMan class
/*
class CMasternodeMan
{
    // ... PIVX implementation ...
};
*/

// TODO: Add PIVX masternode check thread function
/*
void ThreadCheckMasternodes();
*/

#endif // CLORE_MASTERNODEMAN_H