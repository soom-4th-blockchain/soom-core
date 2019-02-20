Soom Core version 1.0.2.0
==========================

Release is now available from:

  <http://foutrhblockchain.org/downloads/>

This is a new major version release, bringing new features and other improvements.

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

### Downgrade to 1.0.1.0

Downgrading to these versions does not require any additional actions, should be
fully compatible.

Notable changes
===============

Additional indexes cover P2PK now
---------------------------------

Additional indexes like `addressindex` etc. process P2PK outputs correctly now. Note, that these indexes will
not be re-built automatically on wallet update, you must reindex manually to update indexes with P2PK outputs.

Support for pruned nodes in Lite Mode
-------------------------------------

It is now possible to run a pruned node which stores only some recent blocks and not the whole blockchain.
However this option is only available in so called Lite Mode. In this mode, Soom specific features are disabled, meaning
that such nodes won't fully validate the blockchain (gateway payments).
InstantSend functions are also disabled on such nodes. Such nodes are comparable to SPV-like nodes
in terms of security and validation - it relies a lot on surrounding nodes, so please keep this in mind if you decide to
use it for something.

RPC changes
-----------

There are a few changes in existing RPC interfaces in this release:
- `gateway count` and `gateway list` will now by default return JSON formatted output;
If you rely on the old output format, you can still specify an additional parameter for backwards compatibility (`all` for `count` and `status` for `list`);
- `gatewaylist` has a few new modes: `daemon`, `json`;
- `debug` rpc now requires categories to be separated via `+`, not `,` like before (e.g. `soom+net`);
- `getchaintips` now shows the block fork occurred in `forkpoint` field;
- `getrawmempool`'s has InstantSend-related info (`instantsend` and `instantlock`);
- `sendrawtransaction` no longer bypasses transaction policy limits by default.
- `dumphdinfo` should throw an error when wallet isn't HD now

There is also a new RPC command `listaddressbalances`.

You can read about RPC changes brought by backporting from Bitcoin Core in following docs:
- https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.13.0.md#low-level-rpc-changes
- https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.13.1.md#low-level-rpc-changes
- https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.14.0.md#low-level-rpc-changes

Command-line options
--------------------

New cmd-line options:
- introduced in Soom Core 1.0.1.0: `allowprivatenet`, `bip9params`, `sporkaddr`;
- backported from Bitcoin Core 0.13/0.14: `blockreconstructionextratxn`, `maxtimeadjustment`, `maxtipage`,
`incrementalrelayfee`, `dustrelayfee`, `blockmintxfee`.

See `Help -> Command-line options` in Qt wallet or `soomd --help` for more info.

New Gateway Information Dialog
---------------------------------

You can now double-click on your gateway in `My Gateways` list on `Gateways` tab to reveal the new
Gateway Information dialog. It will show you some basic information as well as software versions reported by the
gateway. There is also a QR code now which encodes corresponding gateway private key (the one you set with
gwprivkey during GW setup and NOT the one that controls the 5000 DASH collateral).

Testnet fixes
-------------

While we've been in release preparation, a miner used his ASICs on testnet. This resulted in too many blocks being mined
in a too short time. It revealed a few corner-case bugs in validation and synchronisation rules which we have fixed now.
We've also backported a special testnet rule for our difficulty adjustment algorithm that allows to mine a low difficulty
block on testnet when the last block is older than 5 minutes. This and the other fixes should stabilize our testnet in
case of future ASIC uses on testnet.

Using gateway lists for initial peers discovery
--------------------------------------------------

We now use a recent gateway list to feed the hardcoded seed nodes list in Soom Core. This list was previously
unmaintained as we fully relied on DNS based discovery on startup. DNS discovery is still used as the main discovery
method, but the hardcoded seed list should now be able to serve as a proper backup in case DNS fails for some reason.

Lots of backports, refactoring and bug fixes
--------------------------------------------

We backported many performance improvements and refactoring from Bitcoin Core and aligned most of our codebase with version 0.14.
Most notable ones besides various performance and stability improvements probably are
[Compact Block support (BIP 152)](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.13.0.md#compact-block-support-bip-152),
[Mining transaction selection ("Child Pays For Parent")](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.13.0.md#mining-transaction-selection-child-pays-for-parent),
[Null dummy soft fork (BIP 147, without SegWit)](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.13.1.md#null-dummy-soft-fork),
[Nested RPC Commands in Debug Console](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.14.0.md#nested-rpc-commands-in-debug-console) and
[Support for JSON-RPC Named Arguments](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.14.0.md#support-for-json-rpc-named-arguments).

You can read more about all changes in Bitcoin Core 0.13 and 0.14 in following documents:
- [release-notes-0.13.0.md](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.13.0.md);
- [release-notes-0.13.1.md](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.13.1.md);
- [release-notes-0.13.2.md](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.13.2.md);
- [release-notes-0.14.0.md](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.14.0.md);
- [release-notes-0.14.1.md](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.14.1.md);
- [release-notes-0.14.2.md](https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes/release-notes-0.14.2.md).


