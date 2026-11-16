// g++ -m32 -std=c++17 -DASIO_STANDALONE -Itools/harness-support/asio-modern-only -Iap/vendor/websocketpp -Iap/vendor/asio/include tools/test-asio-ssl-includes.cpp -o /tmp/test-asio-ssl-includes -lssl -lcrypto -lpthread
/* OpenSSL 4 makes ASN1_STRING opaque. The pinned Asio umbrella parses an
 * unused deprecated verifier that accesses its fields. Poison that header so
 * this catches the Windows build failure even on hosts with OpenSSL 3. */
#include <websocketpp/common/asio_ssl.hpp>
#include <cassert>
#include <iostream>

int main()
{
    asio::ssl::context context(asio::ssl::context::tls_client);
    context.set_verify_mode(asio::ssl::verify_peer);
    context.set_verify_callback(asio::ssl::host_name_verification("example.invalid"));
    assert(SSL_CTX_get_verify_mode(context.native_handle()) == SSL_VERIFY_PEER);
    std::cout << "WebSocket TLS: modern hostname callback and peer verification compile without deprecated RFC2818 header\n";
}
