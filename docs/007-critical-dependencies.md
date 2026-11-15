# Critical dependency qualification

Dependency ranges describe accepted inputs, not the libraries linked into a
particular binary. Resolve a fresh Conan graph with `conan graph info . --update
--format=json`, retain the graph with build evidence, then build and run tests.
The `rstream-test-ssl-provider` test compares the selected header and runtime
version and writes `ssl-provider.json` in the test directory. Native Conan
packages retain that result under `res/` when tests ran. Cross-built packages
still need execution on their target before claiming runtime qualification.

| Family | Maintenance baseline | Constraint |
| --- | --- | --- |
| OpenSSL | 3.5.9+ on 3.5 LTS, or 3.6.5+ below 4 | Earlier patches and OpenSSL 4 are rejected; a cached 3.6.3 must not satisfy the new graph. |
| LibreSSL | 4.4.1+ below 5 | Public Conan Center currently offers only 3.9.1; its provider graph cannot satisfy this floor. Native builds can use the official LibreSSL CMake package. |
| protobuf | 7.36.2 | Compiler and runtime use the same Conan reference. |
| Boost | Public Conan range remains below 1.90 | Existing Cobalt metadata constraints and reviewed distribution recipe overrides remain in place. |

OpenSSL's [3.6 release notes](https://www.openssl-library.org/news/openssl-3.6-notes/)
and [3.5 release notes](https://www.openssl-library.org/news/openssl-3.5-notes/)
describe the selected patch baselines. A version finding alone does not establish
that a particular rstream call path is exploitable. LibreSSL's supported releases
are listed by [the project](https://www.libressl.org/releases.html).

The LibreSSL build omits OpenSSL ENGINE support, PKCS#11 provider storage and
RPK negotiation;
unsupported configuration fails explicitly. Header search order must match the
selected provider, including when Homebrew exposes a different OpenSSL major in
its general include directory. TLS certificate identity checks remain active
for both DNS names and IP SANs. IP literals are not sent in the DNS-only SNI
extension. The TLS runtime test covers valid and invalid DNS/IP identities,
configuration rejection (including each unsupported RPK direction), handshake/data
transfer and bounded shutdown. Both explicit identities and URL-derived IPv4/IPv6
literals are covered.

The qualification tests do not enable new protocol features or certify a
release. Retain source revisions, provider reports and test results for each
native build matrix; a graph from a different platform is not runtime evidence.
