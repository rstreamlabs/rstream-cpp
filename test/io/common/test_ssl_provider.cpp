// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

int main()
{
  const char* runtime = OpenSSL_version(OPENSSL_VERSION);
  assert(runtime != nullptr);
  assert(std::strcmp(runtime, OPENSSL_VERSION_TEXT) == 0);
  std::ofstream report("ssl-provider.json");
  report << "{\"header\":\"" << OPENSSL_VERSION_TEXT << "\",\"runtime\":\"" << runtime << "\"}\n";
  report.close();
  assert(report.good());
  std::cout << "{\"header\":\"" << OPENSSL_VERSION_TEXT << "\",\"runtime\":\"" << runtime << "\"}\n";
  return 0;
}
