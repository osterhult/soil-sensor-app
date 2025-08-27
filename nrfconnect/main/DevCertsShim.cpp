// nrfconnect/main/DevCertsShim.cpp

// #include <lib/support/Span.h>
// #include <cstddef>
// #include <cstdint>

#include <credentials/examples/ExampleDACs.h> // declares extern ByteSpan in chip::DevelopmentCerts


// namespace chip {
// namespace Credentials {
// namespace Examples {
// // Forward declarations for the arrays & sizes that are DEFINED in ExampleDACs.cpp
// extern const uint8_t kDacCert[];
// extern const size_t  kDacCertSize;
// extern const uint8_t kDacPrivateKey[];
// extern const size_t  kDacPrivateKeySize;
// extern const uint8_t kDacPublicKey[];
// extern const size_t  kDacPublicKeySize;
// } // namespace Examples
// } // namespace Credentials

// namespace DevelopmentCerts {

// // Define the non-const extern ByteSpans promised by ExampleDACs.h
// ByteSpan kDacCert(
//     ::chip::Credentials::Examples::kDacCert,
//     ::chip::Credentials::Examples::kDacCertSize);

// ByteSpan kDacPrivateKey(
//     ::chip::Credentials::Examples::kDacPrivateKey,
//     ::chip::Credentials::Examples::kDacPrivateKeySize);

// ByteSpan kDacPublicKey(
//     ::chip::Credentials::Examples::kDacPublicKey,
//     ::chip::Credentials::Examples::kDacPublicKeySize);

// } // namespace DevelopmentCerts
// } // namespace chip