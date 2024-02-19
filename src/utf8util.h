#pragma once

#include <string>

std::string u8enc(const std::wstring &in);
std::wstring u8dec(const std::string &in);
size_t u8size(const std::string &utf8);
