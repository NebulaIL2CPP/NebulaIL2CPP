#pragma once

// Minimal IL2CPP declarations used by NebulaIL2CPP.
//
// Do not paste a complete Il2CppDumper header into this file: Android
// Studio's C++ indexer will parse every generated game type and may become
// extremely slow. The framework resolves IL2CPP exports dynamically and
// treats runtime metadata handles as opaque pointers.
//
// If a mod needs generated game structures, place the dump outside the
// project or extract only the specific structures into that mod's header.

using Il2CppMethodPointer = void (*)();

struct MethodInfo;
struct Il2CppClass;
struct Il2CppObject;
struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppThread;
struct Il2CppString;
struct Il2CppException;
struct FieldInfo;
