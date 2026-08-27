# Vendored: orlp/ed25519

- Upstream: https://github.com/orlp/ed25519
- Commit: `b1f19fab4aebe607805620d25a5e42566ce46a0e` (master, fetched 2026-08-27)
- License: public domain (`LICENSE` in this directory, copied from upstream `license.txt`)

## Files vendored

From upstream `src/`: `ed25519.h`, `keypair.c`, `sign.c`, `verify.c`, `seed.c`,
`sha512.c`/`.h`, `fe.c`/`.h`, `ge.c`/`.h`, `sc.c`/`.h`, `precomp_data.h`,
`fixedint.h`.

Not vendored (unused by openALE — Location Relay only needs
create_keypair/sign/verify/create_seed): `add_scalar.c`, `key_exchange.c`,
the prebuilt `.dll` binaries, `test.c`, `readme.md`.

## API used

```c
int  ed25519_create_seed(unsigned char *seed);                    // CSPRNG seed (32 bytes)
void ed25519_create_keypair(unsigned char *public_key,            // 32 bytes out
                             unsigned char *private_key,           // 64 bytes out (expanded)
                             const unsigned char *seed);            // 32 bytes in
void ed25519_sign(unsigned char *signature,                        // 64 bytes out
                   const unsigned char *message, size_t message_len,
                   const unsigned char *public_key,
                   const unsigned char *private_key);
int  ed25519_verify(const unsigned char *signature,
                     const unsigned char *message, size_t message_len,
                     const unsigned char *public_key);              // 1 = valid
```

openALE's own CSPRNG (`BCryptGenRandom` on Windows, `/dev/urandom` on POSIX,
wired in `src/App/relay_identity.cpp`) is used instead of
`ed25519_create_seed()`'s built-in seed generator, so the seed source is
consistent with the rest of the codebase and testable; `ed25519_create_seed`
is still vendored (in `seed.c`) but unused.
