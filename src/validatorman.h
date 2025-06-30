// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2021 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_VALIDATORMAN_H
#define CLORE_VALIDATORMAN_H

#include "activevalidator.h"
#include "cyclingvector.h"
#include "key.h"
#include "key_io.h"
#include "net.h"
#include "sync.h"
#include "util/system.h"
#include "validator.h"

#define VALIDATORS_REQUEST_SECONDS (60 * 60) // One hour.

/** Maximum number of block hashes to cache */
static const unsigned int CACHED_BLOCK_HASHES = 200;

// TODO: Add PIVX validator manager class forward declarations
/*
class CValidatorMan;
class CActiveValidator;

extern CValidatorMan mnodeman;
extern CActiveValidator activeValidator;

void DumpValidators();
*/

/** Access to the Validator database (mncache.dat)
 */
// TODO: Add PIVX CValidatorDB class
/*
class CValidatorDB
{
    // ... PIVX implementation ...
};
*/

// TODO: Add PIVX CValidatorMan class
/*
class CValidatorMan
{
    // ... PIVX implementation ...
};
*/

// TODO: Add PIVX validator check thread function
/*
void ThreadCheckValidators();
*/

#endif // CLORE_VALIDATORMAN_H