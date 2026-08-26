#pragma once

class Language
{
private:
	static Language *singleton;
public:
	Language();
    static Language *getInstance();
    wstring getElement(wstring elementId, ...);	// 4J Meow - by value: va_start may not take a reference parameter
	wstring getElement(const wstring& elementId, va_list args);
    wstring getElementName(const wstring& elementId);
    wstring getElementDescription(const wstring& elementId);
};