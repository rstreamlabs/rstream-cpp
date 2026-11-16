# Add WebTTY cipher suites with interoperable clients

The C++ WebTTY implementation uses AES-GCM. Adding another payload algorithm requires a reviewed native implementation, shared Go/JavaScript/C++ vectors and actual client/server interoperability. Keep unsupported suites rejected until negotiation, authentication and cleanup are covered.
