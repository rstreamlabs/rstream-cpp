# Dependency constraints

Dependency ranges describe accepted inputs, not the libraries linked into a
particular binary. Resolve a fresh Conan graph with `conan graph info . --update
--format=json`, retain the graph with build evidence, then build and run tests.
The `rstream-test-ssl-provider` test compares the selected header and runtime
version and writes `ssl-provider.json` in the test directory. Native Conan
packages retain that result under `res/` when tests ran. Cross-built packages
still need execution on their target before claiming runtime qualification.

The [public Conan recipe](../conanfile.py) owns package ranges. CMake checks
native provider versions in [dependencies](../cmake/dependencies.cmake) and the
[OpenSSL version policy](../cmake/ssl-version.cmake). Use those inputs when
resolving a build so the guide stays aligned with the code.

| Family   | Constraint                                                                                                                             |
| -------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| OpenSSL  | Satisfy the supported release range and security patch floor in both Conan and CMake. Cached packages must satisfy the selected graph. |
| LibreSSL | Satisfy the provider floor. A native build can use the official CMake package when a compatible Conan package is unavailable.          |
| protobuf | Use the same Conan reference for compiler and runtime.                                                                                 |
| Boost    | Keep the public recipe range, Asio/Cobalt metadata and target-specific recipe overrides compatible.                                    |

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

Retain source revisions, provider reports and test results for each native
build matrix; a graph from a different platform is not runtime evidence.
