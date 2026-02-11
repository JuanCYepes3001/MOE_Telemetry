// Central image registry for scalable Base64 images
#ifndef IMAGES_H
#define IMAGES_H

// Return a pointer to the Base64 data (without the data:... prefix)
// If name not found, returns an empty string.
const char* get_image_base64(const char* name);

#endif // IMAGES_H
