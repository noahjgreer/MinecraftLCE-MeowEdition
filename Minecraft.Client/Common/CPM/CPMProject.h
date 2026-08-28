#pragma once
// CPM - loads a .cpmproject (the editor's working format) directly.
//
// This is a parallel front end to the binary .cpmmodel loader, not a layer on
// top of it: it reads config.json and skin.png out of the archive and builds
// the same CPMModelDefinition cube tree the binary path produces. Nothing about
// the binary format is involved.
//
// Supporting this means a model can be used without Minecraft Java and the CPM
// mod being installed to export it first.

#include <vector>
#include <string>

class CPMModelDefinition;

// Builds `def` from a .cpmproject image. Returns false and fills `errOut` on
// failure. The caller has already established that the buffer looks like a zip.
bool CPMLoadProject(const unsigned char *file, int len,
                    CPMModelDefinition *def, std::string &errOut);
