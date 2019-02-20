// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2019 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SOOM_QUORUMS_INIT_H
#define SOOM_QUORUMS_INIT_H

class CEvoDB;

namespace llgq
{

void InitLLGQSystem(CEvoDB& evoDb);
void DestroyLLGQSystem();

}

#endif //SOOM_QUORUMS_INIT_H
