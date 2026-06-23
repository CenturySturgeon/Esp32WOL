#pragma once
#ifndef UTILS_H
#define UTILS_H

void url_decode(char *src);
void bytes_to_hex(const unsigned char *src, char *dest, int len);

/**
 * Converts a null-terminated hex string into a binary byte array.
 * 
 * @param hex_str Input hex string (e.g., "a1b2c3")
 * @param output Output buffer for binary data
 * @param max_len Maximum length of output buffer
 * @return Number of bytes written, or -1 on error (invalid hex characters)
 */
int hex_to_bin(const char *hex_str, unsigned char *output, int max_len);

#endif // UTILS_H