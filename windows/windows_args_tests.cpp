#include "windows_args.hpp"
#include <cassert>
#include <iostream>
int main(){
 using synchrorep::winui::quote_windows_argument;
 assert(quote_windows_argument(L"") == L"\"\"");
 assert(quote_windows_argument(L"C:\\Program Files\\Synchrorep") ==
        L"\"C:\\Program Files\\Synchrorep\"");
 assert(quote_windows_argument(L"C:\\") == L"\"C:\\\\\"");
 assert(quote_windows_argument(L"some space") == L"\"some space\"");
 assert(quote_windows_argument(L"a\"b") == L"\"a\\\"b\"");
 std::cout<<"Windows argument quoting tests passed\n";
}
