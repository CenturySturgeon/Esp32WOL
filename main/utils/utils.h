#pragma once
#ifndef UTILS_H
#define UTILS_H

void url_decode(char *src);
void bytes_to_hex(const unsigned char *src, char *dest, int len);

#endif // UTILS_H