#include "images.h"
#include "logo_base64.h"
#include <cstring>

// Simple registry mapping names to image Base64 pointers.
// For scalability add more `extern const char*` definitions in separate .cpp files
// and extend this function.

const char* get_image_base64(const char* name) {
    if (!name) return "";
    // Accept several common keys (case-insensitive): "logo", "logo-moe", "logo-moe.png"
    // Use POSIX strcasecmp if available for robust comparison on different callers.
#if defined(_MSC_VER)
    // Windows/MSVC doesn't have strcasecmp; fallback to case-sensitive compare
    if (std::strcmp(name, "logo") == 0) return logo_base64;
    if (std::strcmp(name, "LOGO-MOE") == 0) return logo_base64;
    if (std::strcmp(name, "LOGO-MOE.png") == 0) return logo_base64;
#else
    if (strcasecmp(name, "logo") == 0) return logo_base64;
    if (strcasecmp(name, "logo-moe") == 0) return logo_base64;
    if (strcasecmp(name, "logo-moe.png") == 0) return logo_base64;
#endif
    return "";
}
