# start-many Setup Guide

## Setting up your Wallet

### Create New Wallet Addresses

1. Open the QT Wallet.
2. Click the Receive tab.
3. Fill in the form to request a payment.
    * Label: gw01
    * Amount: 5000 (optional)
    * Click *Request payment* button
5. Click the *Copy Address* button

Create a new wallet address for each Gateway.

Close your QT Wallet.

### Send 5000 SOOM to New Addresses

Send exactly 5000 SOOM to each new address created above.

### Create New Gateway Private Keys

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Issue the following:

```gateway genkey```

*Note: A gateway private key will need to be created for each Gateway you run. You should not use the same gateway private key for multiple Gateways.*

Close your QT Wallet.

## <a name="gatewayconf"></a>Create gateway.conf file

Remember... this is local. Make sure your QT is not running.

Create the `gateway.conf` file in the same directory as your `wallet.dat`.

Copy the gateway private key and correspondig collateral output transaction that holds the 5000 SOOM.

*Note: The gateway priviate key is **not** the same as a wallet private key. **Never** put your wallet private key in the gateway.conf file. That is almost equivalent to putting your 5000 SOOM on the remote server and defeats the purpose of a hot/cold setup.*

### Get the collateral output

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Issue the following:

```gateway outputs```

Make note of the hash (which is your collateral_output) and index.

### Enter your Gateway details into your gateway.conf file
[From the soom github repo](https://github.com/soom-4th-blockchain/soom-core/blob/master/doc/gateway_conf.md)

`gateway.conf` format is a space seperated text file. Each line consisting of an alias, IP address followed by port, gateway private key, collateral output transaction id and collateral output index.

```
alias ipaddress:port gateway_private_key collateral_output collateral_output_index
```

Example:

```
gw01 127.0.0.1:9999 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0
gw02 127.0.0.2:9999 93WaAb3htPJEV8E9aQcN23Jt97bPex7YvWfgMDTUdWJvzmrMqey aa9f1034d973377a5e733272c3d0eced1de22555ad45d6b24abadff8087948d4 0
```

## Update soom.conf on server

If you generated a new gateway private key, you will need to update the remote `soom.conf` files.

Shut down the daemon and then edit the file.

```nano .soomcore/soom.conf```

### Edit the gatewayprivkey
If you generated a new gateway private key, you will need to update the `gatewayprivkey` value in your remote `soom.conf` file.

## Start your Gateways

### Remote

If your remote server is not running, start your remote daemon as you normally would. 

You can confirm that remote server is on the correct block by issuing

```soom-cli getinfo```

and comparing with the official explorer at explorer.soomcoin.net/insight

### Local

Finally... time to start from local.

#### Open up your QT Wallet

From the menu select `Tools` => `Debug Console`

If you want to review your `gateway.conf` setting before starting Gateways, issue the following in the Debug Console:

```gateway list-conf```

Give it the eye-ball test. If satisfied, you can start your Gateways one of two ways.

1. `gateway start-alias [alias_from_gateway.conf]`  
Example ```gateway start-alias gw01```
2. `gateway start-many`

## Verify that Gateways actually started

### Remote

Issue command `Gateway status`
It should return you something like that:
```
soom-cli gateway status
{
    "outpoint" : "<collateral_output>-<collateral_output_index>",
    "service" : "<ipaddress>:<port>",
    "payee" : "<5000 SOOM address>",
    "status" : "Gateway successfully started"
}
```
Command output should have "_Gateway successfully started_" in its `status` field now. If it says "_not capable_" instead, you should check your config again.

