#pragma once
// CPM - Custom Player Models
//
// The only platform-dependent part of the port: listing and reading .cpmmodel
// files from a folder. Everything else in Common/CPM is platform-independent.
// Consoles have no user-writable model folder, so they get a stub that reports
// an empty list and the rest of the system simply never activates.

#include <vector>
#include <string>

// Lists the model names (file name without the .cpmmodel extension) found in
// the model folder. Returns an empty list when the folder does not exist.
void CPMListModels(std::vector<std::wstring> &out);

// Reads one model by name. Returns false if it is missing or unreadable.
bool CPMReadModel(const std::wstring &name, std::vector<unsigned char> &out);

// The folder models are read from, for logging and error messages.
std::wstring CPMGetModelFolder();
