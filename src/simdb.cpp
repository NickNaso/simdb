#include "../simdb.hpp"

#if defined(_WIN32)
  #define SIMDB_API __declspec(dllexport)
#else
  #define SIMDB_API __attribute__((visibility("default")))
#endif

// A simple exported function to ensure all compiler linkers (especially MSVC) 
// correctly populate entirely valid dynamic (.dll/.so) and static (.lib/.a) library 
// archives even though simdb uses a header-all-inline architecture.
extern "C" SIMDB_API const char* simdb_lib_version() {
    return "1.0.0";
}
