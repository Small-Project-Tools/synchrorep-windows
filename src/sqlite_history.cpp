#include "synchrorep/sqlite_history.hpp"
#include <sqlite3.h>
#include <memory>
#include <stdexcept>
namespace synchrorep {
namespace {
struct Connection {
 sqlite3* db=nullptr;
 explicit Connection(const std::filesystem::path& path){
  const auto bytes=path.u8string();
  const std::string filename(bytes.begin(),bytes.end());
  if(sqlite3_open_v2(filename.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK){
   auto message=db?sqlite3_errmsg(db):"out of memory";
   std::string error(message);
   if(db)sqlite3_close(db);
   throw std::runtime_error("SQLite open: "+error);
  }
  sqlite3_busy_timeout(db,5000);
 }
 ~Connection(){sqlite3_close(db);}
 void exec(const char* sql)const{
  char* message=nullptr;
  if(sqlite3_exec(db,sql,nullptr,nullptr,&message)!=SQLITE_OK){
   std::string error=message?message:"SQLite execution failed";
   sqlite3_free(message);
   throw std::runtime_error(error);
  }
 }
};
struct Statement {
 sqlite3_stmt* stmt=nullptr;
 Statement(sqlite3* db,const char* query){
  if(sqlite3_prepare_v2(db,query,-1,&stmt,nullptr)!=SQLITE_OK)
   throw std::runtime_error(sqlite3_errmsg(db));
 }
 ~Statement(){sqlite3_finalize(stmt);}
 void bind(int index,const std::string& value){
  if(sqlite3_bind_text(stmt,index,value.c_str(),static_cast<int>(value.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
   throw std::runtime_error("Cannot bind SQLite string");
 }
 void integer(int index,std::int64_t value){
  if(sqlite3_bind_int64(stmt,index,value)!=SQLITE_OK)
   throw std::runtime_error("Cannot bind SQLite integer");
 }
};
void schema(const Connection& conn){
 conn.exec("PRAGMA journal_mode=WAL");
 conn.exec("PRAGMA synchronous=FULL");
 conn.exec("CREATE TABLE IF NOT EXISTS sync_pairs("
           "id INTEGER PRIMARY KEY,left_uri TEXT NOT NULL,right_uri TEXT NOT NULL,"
           "UNIQUE(left_uri,right_uri))");
 conn.exec("CREATE TABLE IF NOT EXISTS files("
           "pair_id INTEGER NOT NULL,file TEXT NOT NULL,"
           "left_exists INTEGER NOT NULL,left_directory INTEGER NOT NULL,"
           "left_size INTEGER NOT NULL,left_date INTEGER NOT NULL,left_hash TEXT NOT NULL,"
           "right_exists INTEGER NOT NULL,right_directory INTEGER NOT NULL,"
           "right_size INTEGER NOT NULL,right_date INTEGER NOT NULL,right_hash TEXT NOT NULL,"
           "PRIMARY KEY(pair_id,file), FOREIGN KEY(pair_id) REFERENCES sync_pairs(id))");
 conn.exec("CREATE INDEX IF NOT EXISTS files_by_pair ON files(pair_id,file)");
}
std::int64_t pair_id(const Connection& c,const std::string& left,const std::string& right,bool create){
 if(create){
  Statement insert(c.db,"INSERT OR IGNORE INTO sync_pairs(left_uri,right_uri) VALUES(?,?)");
  insert.bind(1,left);insert.bind(2,right);
  if(sqlite3_step(insert.stmt)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(c.db));
 }
 Statement find(c.db,"SELECT id FROM sync_pairs WHERE left_uri=? AND right_uri=?");
 find.bind(1,left);find.bind(2,right);
 return sqlite3_step(find.stmt)==SQLITE_ROW?sqlite3_column_int64(find.stmt,0):0;
}
std::string column(sqlite3_stmt* s,int index){
 auto* value=sqlite3_column_text(s,index);
 return value?reinterpret_cast<const char*>(value):"";
}
}
SqliteHistory::SqliteHistory(std::filesystem::path file):file_(std::move(file)){}
StoredHistory SqliteHistory::load(const std::string& left,const std::string& right)const{
 if(!std::filesystem::exists(file_))return {};
 Connection c(file_);schema(c);
 auto id=pair_id(c,left,right,false);
 if(!id)return {};
 Statement query(c.db,"SELECT file,left_exists,left_directory,left_size,left_date,left_hash,"
                       "right_exists,right_directory,right_size,right_date,right_hash "
                       "FROM files WHERE pair_id=? ORDER BY file");
 query.integer(1,id);
 StoredHistory result;
 for(int code;(code=sqlite3_step(query.stmt))==SQLITE_ROW;){
  auto name=column(query.stmt,0);
  if(name.empty()||name.front()=='/'||name.find("..")!=std::string::npos)
   throw std::runtime_error("Invalid stored relative path");
  StoredPair p;
  p.left={sqlite3_column_int(query.stmt,1)!=0,sqlite3_column_int(query.stmt,2)!=0,
   static_cast<std::uintmax_t>(sqlite3_column_int64(query.stmt,3)),sqlite3_column_int64(query.stmt,4),column(query.stmt,5)};
  p.right={sqlite3_column_int(query.stmt,6)!=0,sqlite3_column_int(query.stmt,7)!=0,
   static_cast<std::uintmax_t>(sqlite3_column_int64(query.stmt,8)),sqlite3_column_int64(query.stmt,9),column(query.stmt,10)};
  result.emplace(std::move(name),std::move(p));
 }
 return result;
}
void SqliteHistory::save(const std::string& left,const std::string& right,
                         const StoredHistory& entries)const{
 if(!file_.parent_path().empty())std::filesystem::create_directories(file_.parent_path());
 Connection c(file_);schema(c);
 c.exec("BEGIN IMMEDIATE");
 try{
  auto id=pair_id(c,left,right,true);
  Statement erase(c.db,"DELETE FROM files WHERE pair_id=?");
  erase.integer(1,id);
  if(sqlite3_step(erase.stmt)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(c.db));
  Statement insert(c.db,"INSERT INTO files VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
  for(const auto& [name,p]:entries){
   if(name.empty()||name.front()=='/'||name.find("..")!=std::string::npos)
    throw std::runtime_error("Invalid relative path");
   sqlite3_reset(insert.stmt);sqlite3_clear_bindings(insert.stmt);
   insert.integer(1,id);insert.bind(2,name);
   insert.integer(3,p.left.exists);insert.integer(4,p.left.directory);
   insert.integer(5,static_cast<std::int64_t>(p.left.size));insert.integer(6,p.left.modified);
   insert.bind(7,p.left.hash);
   insert.integer(8,p.right.exists);insert.integer(9,p.right.directory);
   insert.integer(10,static_cast<std::int64_t>(p.right.size));insert.integer(11,p.right.modified);
   insert.bind(12,p.right.hash);
   if(sqlite3_step(insert.stmt)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(c.db));
  }
  c.exec("COMMIT");
 }catch(...){try{c.exec("ROLLBACK");}catch(...){}throw;}
}
}
