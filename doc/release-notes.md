Soom Core version 1.0.4.0
==========================

Release is now available from:

  <http://foutrhblockchain.org>

This is a new major version release, bringing new features, various bugfixes and other improvements.

Please report bugs using the issue tracker at github:

  <https://github.com/soom-4th-blockchain/soom-core/issues>


Upgrading and downgrading
=========================

How to Upgrade
--------------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Soom-Qt (on Mac) or
soomd/soom-qt (on Linux).

Downgrade warning
-----------------

### Downgrade to a version < 1.0.4.0

Downgrading to a version smaller than 1.0.4 is only supported as long as DIP2/DIP3
has not been activated. Activation will happen when enough miners signal compatibility
through a BIP9 (bit 3) deployment.

Notable changes
===============

apply dash's DIP2/DIP3/DIP4 improvement.

Mining
------
Please note that gateway payments in `getblocktemplate` rpc are now returned as an array and not as
a single object anymore. Make sure to apply corresponding changes to your pool software.

Also, deterministic gateways can now set their payout address to a P2SH address. The most common use
case for P2SH is multisig but script can be pretty much anything. If your pool software doesn't recognize
P2SH addresses, the simplest way to fix it is to use `script` field which shows scriptPubKey for each
entry of gateway payments array in `getblocktemplate`.

And finally, after DIP0003 activation your pool software must be able to produce coinbase-special-transaction.
Use `coinbase_payload` from `getblocktemplate` to get extra payload needed to construct this transaction.

InstantSend
-----------
With further improvements of networking code it's now possible to handle more load, so we are changing
InstantSend to be always-on for so called "simple txes" - transactions with 4 or less inputs. Such
transactions will be automatically locked even if they only pay minimal fee. According to stats, this
means that up to 90% of currently observed transactions will became automatically locked via InstantSend
with no additional cost to end users or any additional effort from wallet developers or other service
providers.

This feature is going to be activated via combination of a BIP9-like deployment (we are reusing bit 3)
and new spork (`SPORK_16_INSTANTSEND_AUTOLOCKS`).

Historically, InstantSend transactions were shown in GUI and RPC with more confirmations than regular ones,
which caused quite a bit of confusion. This will no longer be the case, instead we are going to show real
blockchain confirmations only and a separate indicator to show if transaction was locked via InstantSend
or not. For GUI it's color highlight and a new column, for RPC commands - `instantlock` field and `addlocked`
param.

One of the issues with InstantSend adoption by SPV wallets (besides lack of Deterministic Gateway List)
was inability to filter all InstantSend messages the same way transactions are filtered. This should be
fixed now and SPV wallets should only get lock votes for transactions they are interested in.

Another popular request was to preserve information about InstantSend locks between wallet restarts, which
is now implemented. This data is stored in a new cache file `instantsend.dat`. You can safely remove it,
if you don't need information about recent transaction locks for some reason (NOTE: make sure it's not one
of your wallets!).

We also added new ZMQ notifications for double-spend attempts which try to override transactions locked
via InstantSend - `zmqpubrawinstantsenddoublespend` and `zmqpubhashinstantsenddoublespend`.

Sporks
------
There are a couple of new sporks introduced in this version `SPORK_15_DETERMINISTIC_GWS_ENABLED` (block
based) and `SPORK_16_INSTANTSEND_AUTOLOCKS` (timestamp based). There is aslo `SPORK_17_QUORUM_DKG_ENABLED`
(timestamp based) which is going to be used on testnet only for now.

Spork data is stored in a new cache file (`sporks.dat`) now.

GUI changes
-----------
Gateways tab has a new section dedicated to DIP0003 registered gateways now. After DIP0003 activation
this will be the only section shown here, the two old sections for non-deterministic gateways will no
longer be available.

There are changes in the way InstantSend transactions are displayed, see `InstantSend` section above.

Some other (mostly minor) issues were also fixed, see `GUI` part of `1.0.4.0 Change log` section below for
detailed list of fixes.

RPC changes
-----------
There are a few changes in existing RPC interfaces in this release:
- `gateway status` and `gateway list` show some DIP0003 related info now;
- `previousbits` and `coinbase_payload` fields were added in `getblocktemplate`;
- `getblocktemplate` now returns an array for gateway payments instead of a single object (miners and mining pools have to upgrade their software to support multiple gateway payees);
- gateway and foundation payments in `getblocktemplate` show payee scriptPubKey in `script` field in addition to payee address in `payee`;
- `getblockchaininfo` shows BIP9 deployment progress;
- `help command subCommand` should give detailed help for subcommands e.g. `help protx list`;
- `compressed` option in `gateway genkey`;
- `dumpwallet` shows info about dumped wallet and warns user about security issues;
- `instantlock` field added in output of `getrawmempool`, `getmempoolancestors`, `getmempooldescendants`, `getmempoolentry`,
`getrawtransaction`, `decoderawtransaction`, `gettransaction`, `listtransactions`, `listsinceblock`;
- `addlocked` param added to `getreceivedbyaddress`, `getreceivedbyaccount`, `getbalance`, `sendfrom`, `sendmany`,
`listreceivedbyaddress`, `listreceivedbyaccount`, `listaccounts`.

There are also new RPC commands:
- `protx` (`list`, `info`, `diff`, `register`, `register_fund`, `register_prepare`,
`register_submit`, `update_service`, `update_registrar`, `revoke`);
- `bls generate`.

See `help command` in rpc for more info.

Command-line options
--------------------

New cmd-line options:
- `gatewayblsprivkey`;
- `minsporkkeys`;
- `zmqpubrawinstantsenddoublespend`;
- `zmqpubhashinstantsenddoublespend`.

Some of them are Devnet only:
- `minimumdifficultyblocks`;
- `highsubsidyblocks`;
- `highsubsidyfactor`.

Few cmd-line options are no longer supported:
- `instantsenddepth`;
- `mempoolreplacement`.

See `Help -> Command-line options` in Qt wallet or `soomd --help` for more info.

Lots of refactoring and bug fixes
---------------------------------

A lot of refactoring, code cleanups and other small fixes were done in this release.

1.0.4.0 Change log
===================

See detailed [set of changes]


