// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/sha256.hpp"
#include <array>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace synchrorep {
namespace {
constexpr std::array<std::uint32_t,64> k={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
std::uint32_t rotate(std::uint32_t v,unsigned n){return (v>>n)|(v<<(32-n));}
}
Sha256::Sha256():state_{
 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
}{}
void Sha256::block(const unsigned char* bytes){
 std::array<std::uint32_t,64> words{};
 for(unsigned i=0;i<16;++i){
  words[i]=(std::uint32_t(bytes[i*4])<<24)|(std::uint32_t(bytes[i*4+1])<<16)|
           (std::uint32_t(bytes[i*4+2])<<8)|std::uint32_t(bytes[i*4+3]);
 }
 for(unsigned i=16;i<64;++i){
  const auto x=words[i-15],y=words[i-2];
  words[i]=(rotate(y,17)^rotate(y,19)^(y>>10))+words[i-7]+
            (rotate(x,7)^rotate(x,18)^(x>>3))+words[i-16];
 }
 auto a=state_[0],b=state_[1],c=state_[2],d=state_[3];
 auto e=state_[4],f=state_[5],g=state_[6],h=state_[7];
 for(unsigned i=0;i<64;++i){
  auto t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+words[i];
  auto t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
  h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
 }
 state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;
 state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
}
void Sha256::update(const unsigned char* data,std::size_t length){
 if(finished_)throw std::logic_error("SHA-256 already finalized");
 if(length>0&&!data)throw std::invalid_argument("Null SHA-256 input");
 if(length>(std::numeric_limits<std::uint64_t>::max()/8)-bytes_)
  throw std::length_error("SHA-256 input too long");
 bytes_+=length;
 if(buffered_!=0){
  const auto bytes=std::min(length,64-buffered_);
  std::copy_n(data,bytes,buffer_.begin()+buffered_);
  data+=bytes;length-=bytes;buffered_+=bytes;
  if(buffered_==64){block(buffer_.data());buffered_=0;}
 }
 while(length>=64){block(data);data+=64;length-=64;}
 if(length){std::copy_n(data,length,buffer_.begin());buffered_=length;}

}
std::string Sha256::finish(){
 if(finished_)throw std::logic_error("SHA-256 already finalized");
 finished_=true;
 const auto bits=bytes_*8;
 buffer_[buffered_++]=0x80;
 if(buffered_>56){
  std::fill(buffer_.begin()+buffered_,buffer_.end(),0);
  block(buffer_.data());buffered_=0;
 }
 std::fill(buffer_.begin()+buffered_,buffer_.begin()+56,0);
 for(unsigned i=0;i<8;++i)buffer_[56+i]=static_cast<unsigned char>(bits>>(56-8*i));
 block(buffer_.data());
 std::ostringstream output;output<<std::hex<<std::setfill('0');
 for(const auto word:state_)output<<std::setw(8)<<word;
 return output.str();
}
std::string sha256(std::istream& input,std::uintmax_t expected_size){
 Sha256 hash;
 std::uintmax_t total=0;
 std::array<char,65536> buffer{};
 for(;;){
  input.read(buffer.data(),buffer.size());
  const auto length=input.gcount();
  if(input.bad())throw std::runtime_error("Read failed during SHA-256 calculation");
  if(length>0){
   const auto size=static_cast<std::uintmax_t>(length);
   if(size>expected_size-total)throw std::runtime_error("File changed while hashing (larger than listing)");
   hash.update(reinterpret_cast<const unsigned char*>(buffer.data()),size);
   total+=size;
  }
  if(input.eof())break;
  if(!input)throw std::runtime_error("Incomplete file read during SHA-256 calculation");
 }
 if(total!=expected_size)throw std::runtime_error("File changed while hashing (size mismatch)");
 return hash.finish();
}
}
