# Networking boundary checks

Run `bash tools/security/run.sh` with the pinned vendor trees present, a 32-bit C++ toolchain, OpenSSL development libraries, zlib and Python 3. The fixtures use temporary certificates, loopback sockets and an isolated cache. CI runs them alongside the registered native harnesses.

The TLS test exercises the actual WebSocket transport: a trusted matching IP certificate succeeds, a trusted wrong-name certificate fails, and an untrusted matching certificate fails. Repeated fresh transport instances check that verification is installed each time. This does not simulate a full Archipelago reconnect. The pinned Asio verifier checks DNS names with `X509_check_host` and IP addresses with `X509_check_ip_asc`.

The cache test exercises the actual default store, including valid read/write and legacy entries, dot components, separators, trailing dots/spaces and control characters. It rejects remote path-component escapes; it does not defend against a local user replacing cache directories with symlinks.

The existing literal localhost certificate exception remains: `localhost`, `127.0.0.1` and `::1`. Explicit `wss://` should be used when encryption is required; the upstream client's scheme-less address negotiation is unchanged.

Security corrections to the pinned upstream dependencies are committed under `ap/vendor/patches/`. Fetching verifies the upstream tree before applying the exact hash-pinned patch, then verifies the final tree. Configure verifies that same final tree. Offline `verify` never modifies a tree. Dependencies otherwise remain at their existing upstream commits.
