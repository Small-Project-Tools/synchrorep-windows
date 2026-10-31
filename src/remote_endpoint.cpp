#include "synchrorep/remote_endpoint.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace synchrorep {
namespace {
constexpr std::size_t kMaxListingBytes = 32 * 1024 * 1024;
std::once_flag global_init;
using CurlHandle = std::unique_ptr<CURL,decltype(&curl_easy_cleanup)>;
using CurlList = std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)>;

int cancel_curl(void* opaque,curl_off_t,curl_off_t,curl_off_t,curl_off_t){
 auto& check=*static_cast<std::function<bool()>*>(opaque);
 return check&&check()?1:0;
}
void attach_cancellation(CURL* curl,const std::function<bool()>& check){
 if(!check)return;
 curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
 curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,cancel_curl);
 curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&check);
}
void verify_curl(CURLcode status,const std::function<bool()>& check,const std::string& action){
 if(status==CURLE_ABORTED_BY_CALLBACK && check && check())throw RunCancelled();
 if(status!=CURLE_OK)throw std::runtime_error(action+": "+curl_easy_strerror(status));
}
void initialize(){
 std::call_once(global_init, []{if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)
  throw std::runtime_error("Cannot initialize libcurl");});
}
std::string validate_relative(const std::string& name,bool empty_ok=false){
 if(empty_ok&&name.empty())return {};
 if(name.empty()||name.front()=='/'||name.find('\\')!=std::string::npos||
    name.find('\r')!=std::string::npos||name.find('\n')!=std::string::npos)
  throw std::invalid_argument("Unsafe remote relative name");
 std::stringstream stream(name);
 std::string part;
 while(std::getline(stream,part,'/')){
  if(part.empty()||part=="."||part=="..")throw std::invalid_argument("Unsafe remote path component");
 }
 if(name.back()=='/')throw std::invalid_argument("Unsafe remote trailing slash");
 return name;
}
std::string escape_part(const std::string& text){
 initialize();
 CurlHandle curl(curl_easy_init(),curl_easy_cleanup);
 if(!curl)throw std::runtime_error("Cannot allocate URL escaper");
 char* escaped=curl_easy_escape(curl.get(),text.c_str(),static_cast<int>(text.size()));
 if(!escaped)throw std::runtime_error("Failed encoding remote path");
 std::string result(escaped);curl_free(escaped);
 return result;
}
std::string url_path(const std::string& relative){
 std::stringstream in(relative);
 std::string part,result;
 while(std::getline(in,part,'/')){
  if(!result.empty())result+='/';
  result+=escape_part(part);
 }
 return result;
}
std::string trim_cr(std::string value){
 if(!value.empty()&&value.back()=='\r')value.pop_back();
 return value;
}
std::int64_t parse_ftp_time(const std::string& text){
 if(text.size()<14)throw std::runtime_error("FTP MLSD entry without valid modify time");
 auto digits=[&](std::size_t start,std::size_t len)->int{
  for(std::size_t i=start;i<start+len;i++)if(text[i]<'0'||text[i]>'9')
   throw std::runtime_error("Invalid FTP MLSD timestamp");
  return std::stoi(text.substr(start,len));
 };
 using namespace std::chrono;
 const year_month_day date{year{digits(0,4)},month{unsigned(digits(4,2))},day{unsigned(digits(6,2))}};
 if(!date.ok())throw std::runtime_error("Invalid FTP date");
 int hour=digits(8,2),min=digits(10,2),sec=digits(12,2);
 if(hour>23||min>59||sec>59)throw std::runtime_error("Invalid FTP time");
 return duration_cast<seconds>(sys_days{date}.time_since_epoch()).count()+hour*3600+min*60+sec;
}
struct FTPItem{std::string name;Entry entry;};
FTPItem parse_ftp_line(const std::string& line){
 auto sep=line.find(' ');
 if(sep==std::string::npos||sep+1>=line.size())throw std::runtime_error("Unrecognized FTP MLSD entry");
 std::string facts=line.substr(0,sep),name=line.substr(sep+1);
 if(!name.empty()&&name.front()==' ')name.erase(name.begin());
 if(name=="."||name=="..")return {name,{}};
 validate_relative(name);
 std::string type,modify;
 std::uintmax_t size=0;
 bool size_found=false;
 std::stringstream stream(facts);std::string fact;
 while(std::getline(stream,fact,';')){
  auto eq=fact.find('=');
  if(eq==std::string::npos)continue;
  auto key=fact.substr(0,eq),val=fact.substr(eq+1);
  std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
  if(key=="type")type=val;
  if(key=="modify")modify=val;
  if(key=="size"){size=std::stoull(val);size_found=true;}
 }
 if(type=="cdir"||type=="pdir")return {".",{}};
 if(type!="file"&&type!="dir")throw std::runtime_error("Unsupported FTP entry type (possibly symlink): "+name);
 Entry e{true,type=="dir",size,0,{}};
 if(!e.directory){
  if(!size_found||modify.empty())throw std::runtime_error("FTP MLSD file missing size/modify facts: "+name);
  e.modified=parse_ftp_time(modify);
 }
 return {name,e};
}
struct SftpItem {std::string name;bool directory=false;std::uintmax_t size=0;};
SftpItem parse_sftp_line(const std::string& line){
 // SFTP server LONGNAME is not a guaranteed machine-readable format;
 // accept only common OpenSSH/POSIX style, fail closed otherwise.
 static const std::regex pattern(R"(^([dl\-])[rwxstST\-]{9}\+?\s+\d+\s+\S+\s+\S+\s+(\d+)\s+\S+\s+\S+\s+\S+\s+(.+)$)");
 std::smatch match;
 if(!std::regex_match(line,match,pattern))throw std::runtime_error("Unsupported SFTP directory listing format");
 std::string name=match[3].str();
 if(match[1]=="l")throw std::runtime_error("SFTP symbolic link not supported: "+name);
 if(name=="."||name=="..")return {name,false,0};
 validate_relative(name);
 return {name,match[1]=="d",std::stoull(match[2].str())};
}
size_t write_string(char* data,size_t size,size_t n,void* opaque){
 auto* out=static_cast<std::string*>(opaque);
 if(size&&n>SIZE_MAX/size)return 0;
 const auto len=size*n;
 if(out->size()+len>kMaxListingBytes)return 0;
 out->append(data,len);return len;
}
size_t write_file(char* data,size_t size,size_t n,void* opaque){
 auto* out=static_cast<std::ofstream*>(opaque);
 if(size&&n>SIZE_MAX/size)return 0;
 out->write(data,static_cast<std::streamsize>(size*n));
 return *out?size*n:0;
}
struct UploadReader {std::istream* input;std::uintmax_t read=0;};
size_t read_file(char* target,size_t size,size_t n,void* opaque){
 auto* state=static_cast<UploadReader*>(opaque);
 auto* in=state->input;
 if(size&&n>SIZE_MAX/size)return CURL_READFUNC_ABORT;
 in->read(target,static_cast<std::streamsize>(size*n));
 if(in->bad())return CURL_READFUNC_ABORT;
 state->read+=static_cast<std::uintmax_t>(in->gcount());
 return static_cast<size_t>(in->gcount());
}
class TemporaryInputStream final:public std::ifstream {
public:
 explicit TemporaryInputStream(std::filesystem::path path)
 :std::ifstream(path,std::ios::binary),path_(std::move(path)){}
 ~TemporaryInputStream()override {close();std::error_code ec;std::filesystem::remove(path_,ec);}
private:
 std::filesystem::path path_;
};
std::filesystem::path temporary_path(){
 static std::atomic<std::uint64_t> seq{0};
 const auto count=std::chrono::steady_clock::now().time_since_epoch().count();
 return fs::temp_directory_path()/("synchrorep-download-"+std::to_string(count)+"-"+std::to_string(seq++));
}
}
RemoteEndpoint::RemoteEndpoint(std::string url,RemoteCredentials credentials)
:root_(std::move(url)),credentials_(std::move(credentials)){
 if(root_.rfind("ftp://",0)==0)scheme_="ftp";
 else if(root_.rfind("sftp://",0)==0)scheme_="sftp";
 else throw std::invalid_argument("Only FTP and SFTP URL schemes are allowed");
 if(root_.find_first_of("?#")!=std::string::npos||root_.find('@')!=std::string::npos||
    root_.find('\r')!=std::string::npos||root_.find('\n')!=std::string::npos)
  throw std::invalid_argument("Remote URI must not contain credentials, query or fragment");
 auto authority_end=root_.find('/',scheme_.size()+3);
 auto authority=root_.substr(scheme_.size()+3,authority_end==std::string::npos?std::string::npos:authority_end-scheme_.size()-3);
 if(authority.empty()||authority.find(' ')!=std::string::npos)
  throw std::invalid_argument("Invalid remote host");
 remote_path_=authority_end==std::string::npos?"/":root_.substr(authority_end);
 if(remote_path_.find('%')!=std::string::npos)throw std::invalid_argument("Encoded remote root unsupported");
 if(remote_path_.back()!='/')remote_path_+='/';
 // Never print the supplied username/password or include them in identity().
 root_=scheme_+"://"+authority+remote_path_;
 if(credentials_.username.empty())throw std::invalid_argument("Missing remote username");
 if(scheme_=="sftp"&&(credentials_.known_hosts.empty()||!fs::is_regular_file(credentials_.known_hosts)))
  throw std::invalid_argument("SFTP requires a configured known_hosts file");
 initialize();
 const auto* info=curl_version_info(CURLVERSION_NOW);
 bool supported=false;
 for(const char* const* p=info->protocols; p&&*p;p++)if(scheme_==*p)supported=true;
 if(!supported)throw std::runtime_error("This libcurl was built without "+scheme_+" support");
}
std::string RemoteEndpoint::identity()const {return root_;}
void RemoteEndpoint::set_cancellation(std::function<bool()> callback)const {
 cancelled_=std::move(callback);
}
std::string RemoteEndpoint::url_for(const std::string& relative,bool directory)const{
 std::string u=root_;
 if(!relative.empty())u+=url_path(validate_relative(relative));
 if(directory&&u.back()!='/')u+='/';
 return u;
}
std::string RemoteEndpoint::quote_path(const std::string& relative)const {
 validate_relative(relative);
 std::string absolute=remote_path_+relative;
 if(scheme_=="ftp"){
  if(absolute.find('"')!=std::string::npos)throw std::invalid_argument("FTP quote cannot handle double quote");
  return absolute;
 }
 std::string escaped="\"";
 for(char c:absolute){if(c=='"'||c=='\\')escaped+='\\';escaped+=c;}
 escaped+='\"';
 return escaped;
}
void RemoteEndpoint::command(const std::vector<std::string>& commands,bool allow_cancel)const{
 CurlHandle curl(curl_easy_init(),curl_easy_cleanup);
 if(!curl)throw std::runtime_error("Cannot create remote command");
 curl_easy_setopt(curl.get(),CURLOPT_URL,root_.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_NOBODY,1L);
 curl_easy_setopt(curl.get(),CURLOPT_USERNAME,credentials_.username.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_PASSWORD,credentials_.password.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_PROTOCOLS_STR,scheme_.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT,15L);
 curl_easy_setopt(curl.get(),CURLOPT_TIMEOUT,120L);
 curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L);
 if(scheme_=="sftp"){
  curl_easy_setopt(curl.get(),CURLOPT_SSH_KNOWNHOSTS,credentials_.known_hosts.c_str());
  if(!credentials_.private_key.empty())curl_easy_setopt(curl.get(),CURLOPT_SSH_PRIVATE_KEYFILE,credentials_.private_key.c_str());
 }
 curl_slist* raw=nullptr;
 for(const auto& command:commands)raw=curl_slist_append(raw,command.c_str());
 CurlList quotes(raw,curl_slist_free_all);
 curl_easy_setopt(curl.get(),CURLOPT_QUOTE,quotes.get());
 if(allow_cancel)attach_cancellation(curl.get(),cancelled_);
 verify_curl(curl_easy_perform(curl.get()),allow_cancel?cancelled_:std::function<bool()>{},"Remote command failed");
}
void RemoteEndpoint::scan_directory(const std::string& relative,Snapshot& result,unsigned depth)const {
 if(cancelled_&&cancelled_())throw RunCancelled();
 if(depth>80)throw std::runtime_error("Remote nesting too deep");
 CurlHandle curl(curl_easy_init(),curl_easy_cleanup);
 if(!curl)throw std::runtime_error("Cannot create remote listing request");
 std::string listing,u=url_for(relative,true);
 curl_easy_setopt(curl.get(),CURLOPT_URL,u.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_USERNAME,credentials_.username.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_PASSWORD,credentials_.password.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_PROTOCOLS_STR,scheme_.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT,15L);
 curl_easy_setopt(curl.get(),CURLOPT_TIMEOUT,120L);
 curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(curl.get(),CURLOPT_WRITEFUNCTION,write_string);
 curl_easy_setopt(curl.get(),CURLOPT_WRITEDATA,&listing);
 if(scheme_=="ftp")curl_easy_setopt(curl.get(),CURLOPT_CUSTOMREQUEST,"MLSD");
 else curl_easy_setopt(curl.get(),CURLOPT_SSH_KNOWNHOSTS,credentials_.known_hosts.c_str());
 if(scheme_=="sftp"&&!credentials_.private_key.empty())
  curl_easy_setopt(curl.get(),CURLOPT_SSH_PRIVATE_KEYFILE,credentials_.private_key.c_str());
 attach_cancellation(curl.get(),cancelled_);
 verify_curl(curl_easy_perform(curl.get()),cancelled_,"Remote directory listing failed (no deletions performed)");
 std::stringstream lines(listing);std::string line;
 std::vector<std::string> dirs;
 while(std::getline(lines,line)){
  line=trim_cr(line);if(line.empty())continue;
  Entry entry{};std::string name;
  if(scheme_=="ftp"){
   auto p=parse_ftp_line(line);name=p.name;entry=p.entry;
   if(name=="."||name=="..")continue;
  }else{
   auto p=parse_sftp_line(line);name=p.name;
   if(name=="."||name=="..")continue;
   entry.exists=true;entry.directory=p.directory;entry.size=p.size;
   if(!entry.directory){
    // Filetime is obtained from the remote server's SFTP stat operation.
    CurlHandle f(curl_easy_init(),curl_easy_cleanup);
    auto key=relative.empty()?name:relative+"/"+name;
    auto f_url=url_for(key);
    curl_easy_setopt(f.get(),CURLOPT_URL,f_url.c_str());
    curl_easy_setopt(f.get(),CURLOPT_USERNAME,credentials_.username.c_str());
    curl_easy_setopt(f.get(),CURLOPT_PASSWORD,credentials_.password.c_str());
    curl_easy_setopt(f.get(),CURLOPT_PROTOCOLS_STR,"sftp");
    curl_easy_setopt(f.get(),CURLOPT_SSH_KNOWNHOSTS,credentials_.known_hosts.c_str());
    if(!credentials_.private_key.empty())curl_easy_setopt(f.get(),CURLOPT_SSH_PRIVATE_KEYFILE,credentials_.private_key.c_str());
    curl_easy_setopt(f.get(),CURLOPT_NOBODY,1L);
    curl_easy_setopt(f.get(),CURLOPT_FILETIME,1L);
    curl_easy_setopt(f.get(),CURLOPT_CONNECTTIMEOUT,15L);
    curl_easy_setopt(f.get(),CURLOPT_TIMEOUT,120L);
    curl_easy_setopt(f.get(),CURLOPT_NOSIGNAL,1L);
    attach_cancellation(f.get(),cancelled_);
    verify_curl(curl_easy_perform(f.get()),cancelled_,"Cannot stat SFTP file");
    curl_off_t length=-1,modified=-1;
    curl_easy_getinfo(f.get(),CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&length);
    curl_easy_getinfo(f.get(),CURLINFO_FILETIME_T,&modified);
    if(modified<0)throw std::runtime_error("Missing reliable SFTP modification time");
    if(length>=0 && static_cast<std::uintmax_t>(length)!=entry.size)throw std::runtime_error("SFTP listing/stat size mismatch");
    entry.modified=modified;
   }
  }
  // A process killed during upload may leave its staging file on the server.
  // Block synchronization rather than silently copying that partial file.
  static const std::regex unfinished(R"(\.synchrorep-upload-[0-9]+$)");
  if(std::regex_search(name,unfinished))
   throw std::runtime_error("Interrupted remote upload artifact detected; review and remove manually: "+name);
  auto key=relative.empty()?name:relative+"/"+name;
  if(!result.emplace(key,entry).second)throw std::runtime_error("Duplicate remote path in listing");
  if(entry.directory)dirs.push_back(key);
 }
 for(const auto& d:dirs)scan_directory(d,result,depth+1);
}
Snapshot RemoteEndpoint::scan()const {
 Snapshot result;scan_directory("",result,0);cached_=result;return result;
}
std::unique_ptr<std::istream> RemoteEndpoint::read(const std::string& relative)const {
 if(cancelled_&&cancelled_())throw RunCancelled();
 auto dest=temporary_path();
 try{
  std::ofstream out(dest,std::ios::binary|std::ios::trunc);
  if(!out)throw std::runtime_error("Cannot create download temporary file");
  CurlHandle curl(curl_easy_init(),curl_easy_cleanup);
  auto u=url_for(relative);
  curl_easy_setopt(curl.get(),CURLOPT_URL,u.c_str());
  curl_easy_setopt(curl.get(),CURLOPT_USERNAME,credentials_.username.c_str());
  curl_easy_setopt(curl.get(),CURLOPT_PASSWORD,credentials_.password.c_str());
  curl_easy_setopt(curl.get(),CURLOPT_PROTOCOLS_STR,scheme_.c_str());
  curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT,15L);
  curl_easy_setopt(curl.get(),CURLOPT_TIMEOUT,0L);
  curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L);
  curl_easy_setopt(curl.get(),CURLOPT_WRITEFUNCTION,write_file);
  curl_easy_setopt(curl.get(),CURLOPT_WRITEDATA,&out);
  if(scheme_=="sftp"){
   curl_easy_setopt(curl.get(),CURLOPT_SSH_KNOWNHOSTS,credentials_.known_hosts.c_str());
   if(!credentials_.private_key.empty())curl_easy_setopt(curl.get(),CURLOPT_SSH_PRIVATE_KEYFILE,credentials_.private_key.c_str());
  }
  attach_cancellation(curl.get(),cancelled_);
  auto status=curl_easy_perform(curl.get());
  out.flush();
  verify_curl(status,cancelled_,"Remote download failed");
  if(!out)throw std::runtime_error("Incomplete remote download");
  out.close();
  auto input=std::make_unique<TemporaryInputStream>(dest);
  if(!*input)throw std::runtime_error("Cannot reopen remote download");
  return input;
 }catch(...){std::error_code ec;fs::remove(dest,ec);throw;}
}
void RemoteEndpoint::write(const std::string& relative,std::istream& input,const Entry& source){
 if(cancelled_&&cancelled_())throw RunCancelled();
 validate_relative(relative);
 if(source.directory)throw std::invalid_argument("Cannot write a directory");
 auto parent=relative.find_last_of('/');
 if(parent!=std::string::npos){
  // Parent should have already been created by the engine's directory pass.
  // No unsafe recursive auto-creation by remote servers.
 }
 auto tmp=relative+".synchrorep-upload-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 CurlHandle curl(curl_easy_init(),curl_easy_cleanup);
 auto u=url_for(tmp);
 curl_easy_setopt(curl.get(),CURLOPT_URL,u.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_USERNAME,credentials_.username.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_PASSWORD,credentials_.password.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_PROTOCOLS_STR,scheme_.c_str());
 curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT,15L);
 curl_easy_setopt(curl.get(),CURLOPT_TIMEOUT,0L);
 curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(curl.get(),CURLOPT_UPLOAD,1L);
 curl_easy_setopt(curl.get(),CURLOPT_READFUNCTION,read_file);
 UploadReader reader{&input};
 curl_easy_setopt(curl.get(),CURLOPT_READDATA,&reader);
 curl_easy_setopt(curl.get(),CURLOPT_INFILESIZE_LARGE,static_cast<curl_off_t>(source.size));
 if(scheme_=="sftp"){
  curl_easy_setopt(curl.get(),CURLOPT_SSH_KNOWNHOSTS,credentials_.known_hosts.c_str());
  if(!credentials_.private_key.empty())curl_easy_setopt(curl.get(),CURLOPT_SSH_PRIVATE_KEYFILE,credentials_.private_key.c_str());
 }
 attach_cancellation(curl.get(),cancelled_);
 auto code=curl_easy_perform(curl.get());
 if(code!=CURLE_OK || input.bad() || reader.read!=source.size) {
  try{
   if(scheme_=="ftp")command({"DELE "+quote_path(tmp)},false);
   else command({"rm "+quote_path(tmp)},false);
  }catch(const std::exception&){ /* best effort cleanup; original file untouched */ }
  verify_curl(code,cancelled_,"Remote upload failed; original destination preserved");
  throw std::runtime_error("Remote upload size mismatch; original destination preserved");
 }
 try {
  // Replace existing destination only after the complete upload.
  if(scheme_=="ftp")command({"RNFR "+quote_path(tmp),"RNTO "+quote_path(relative)});
  else command({"rename "+quote_path(tmp)+" "+quote_path(relative)});
 }catch(...){
  try{
   if(scheme_=="ftp")command({"DELE "+quote_path(tmp)},false);
   else command({"rm "+quote_path(tmp)},false);
  }catch(const std::exception&){}
  throw;
 }
}
void RemoteEndpoint::make_directory(const std::string& relative){
 if(cancelled_&&cancelled_())throw RunCancelled();
 auto key=validate_relative(relative);
 if(scheme_=="ftp")command({"MKD "+quote_path(key)});
 else command({"mkdir "+quote_path(key)});
}
void RemoteEndpoint::remove(const std::string& relative){
 if(cancelled_&&cancelled_())throw RunCancelled();
 auto key=validate_relative(relative);
 auto item=cached_.find(key);
 if(item==cached_.end())throw std::runtime_error("Remote delete refused: path not present in last scan");
 if(scheme_=="ftp")command({std::string(item->second.directory?"RMD ":"DELE ")+quote_path(key)});
 else command({std::string(item->second.directory?"rmdir ":"rm ")+quote_path(key)});
}
}
