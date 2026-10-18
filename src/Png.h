#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Speichert ein RGB-Bild (8 Bit pro Kanal) als unkomprimiertes PNG.
bool writePng(const std::string& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgb);
