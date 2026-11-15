#pragma once

// UTS #46 (IDNA compatibility processing) domain-to-ASCII, as WHATWG URL
// parsing requires. Nontransitional mapping (deviation characters such as
// sharp s are kept), NFC normalization, the UTS #46 validity rules (mapping,
// NFC, joiner context, bidi, leading mark) and Punycode.
//
// The domain passed in must already be percent-decoded and decoded to UTF-8
// scalar values (invalid bytes are the caller's concern; pass U+FFFD for
// them — disallowed there, which fails the conversion).
//
// beStrict is the WHATWG "beStrict" flag: false for URL parsing (CheckHyphens,
// UseSTD3ASCIIRules and VerifyDnsLength set to false), true only for the
// strict validation pass a spec-level client may want. Returns false on any
// failure; on success `out` holds the ASCII form (dot-separated labels,
// non-ASCII labels punycode-encoded with the xn-- prefix).

#include <string>
#include <string_view>
#include <cstddef>

namespace litebird {

bool domain_to_ascii(std::string_view utf8_domain, std::string& out, bool beStrict = false);

} // namespace litebird
