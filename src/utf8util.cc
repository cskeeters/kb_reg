#include "utf8util.h"

#include "utf8/checked.h"
#include "utf8/unchecked.h"

using namespace std;
using namespace utf8;

string u8enc(const wstring &in)
{
    string out;
    unchecked::utf16to8(in.c_str(), in.c_str()+in.size(), back_inserter(out));
    return out;
}

wstring u8dec(const string &in)
{
    wstring out;
    unchecked::utf8to16(in.c_str(), in.c_str()+in.size(), back_inserter(out));
    return out;
}

size_t u8size(const string &utf8) {
    return utf8::distance(utf8.c_str(), utf8.c_str()+utf8.size());
}
