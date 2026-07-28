# Configuration

`defaults.env` contains public, non-secret experiment defaults. It is shell
syntax and is sourced by the host-side network, deployment, policy, and
experiment scripts.

For machine-specific settings:

```bash
cp config/local.env.example config/local.env
```

`local.env` is loaded after the defaults and is ignored by Git. Alternatively,
set `AIRPORT_CONFIG` to an absolute override file. Do not place passwords,
tokens, private keys, production Wi-Fi credentials, or other secrets in
`defaults.env`.
