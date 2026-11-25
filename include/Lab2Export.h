\
#pragma once

// Static build: no import/export decoration.
// DLL build: export symbols while compiling the DLL and import them in its clients.
#if defined(_WIN32) && defined(LAB2_BUILD_DLL)
    #define LAB2_API __declspec(dllexport)
#elif defined(_WIN32) && !defined(LAB2_STATIC)
    #define LAB2_API __declspec(dllimport)
#else
    #define LAB2_API
#endif
