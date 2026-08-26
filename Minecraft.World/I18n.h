#pragma once
using namespace std;

#include "stdafx.h"
#include "Language.h"

class I18n
{
private:
	static Language *lang;

public:
    static wstring get(wstring id, ...);	// 4J Meow - by value: va_start may not take a reference parameter
	static wstring get(const wstring& id, va_list args);
};