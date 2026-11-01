// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "synchrorep/engine.hpp"

namespace synchrorep::winui {
constexpr UINT ProgressMessage=WM_APP+51;
constexpr UINT FinishedMessage=WM_APP+52;
struct ProgressEvent {Progress state;};
struct FinishedEvent {
 std::vector<Decision> decisions;
 Result result;
 std::string error;
 bool executed=false;
 bool cancelled=false;
};

class AsyncJob {
public:
 explicit AsyncJob(HWND owner):owner_(owner){}
 ~AsyncJob(){stop();join();}
 AsyncJob(const AsyncJob&)=delete;
 AsyncJob& operator=(const AsyncJob&)=delete;

 void start(std::function<FinishedEvent(const RunCallbacks&)> work) {
  join();
  cancelled_.store(false);
  thread_=std::thread([this,work=std::move(work)]{
   FinishedEvent result;
   try{
    RunCallbacks callbacks;
    callbacks.cancelled=[this]{return cancelled_.load();};
    callbacks.progress=[this](const Progress& progress){
     auto* event=new ProgressEvent{progress};
     if(!PostMessageW(owner_,ProgressMessage,0,reinterpret_cast<LPARAM>(event)))delete event;
    };
    result=work(callbacks);
   }catch(const RunCancelled&){result.cancelled=true;}
    catch(const std::exception& error){result.error=error.what();}
   auto* event=new FinishedEvent(std::move(result));
   if(!PostMessageW(owner_,FinishedMessage,0,reinterpret_cast<LPARAM>(event)))delete event;
  });
 }
 void stop(){cancelled_.store(true);}
 void join(){if(thread_.joinable())thread_.join();}
private:
 HWND owner_=nullptr;
 std::atomic<bool> cancelled_{false};
 std::thread thread_;
};
}
