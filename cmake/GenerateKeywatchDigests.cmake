file(SHA256 "${DAEMON}" daemon_sha256)
file(SHA256 "${POLICY}" policy_sha256)

file(WRITE "${OUTPUT}"
  "#pragma once\n\n"
  "#include <string_view>\n\n"
  "namespace speecher::keywatch {\n"
  "constexpr std::string_view daemonSha256 = \"${daemon_sha256}\";\n"
  "constexpr std::string_view policySha256 = \"${policy_sha256}\";\n"
  "} // namespace speecher::keywatch\n")
