#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>

namespace synchrorep {
class Sha256 {
public:
 Sha256();
 void update(const unsigned char* data,std::size_t length);
 std::string finish();
private:
 void block(const unsigned char* bytes);
 std::array<std::uint32_t,8> state_{};
 std::array<unsigned char,64> buffer_{};
 std::uint64_t bytes_=0;
 std::size_t buffered_=0;
 bool finished_=false;
};
std::string sha256(std::istream& input,std::uintmax_t expected_size);
}
