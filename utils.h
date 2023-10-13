#pragma once
#include <stdint.h>
#include <stdio.h> // sprintf
#include <rpp/strview.h>
#include <math.h> // round

static uint32_t parseSizeLiteral(rpp::strview literal) noexcept
{
    double value = literal.next_double();
    // and parse the remaining unit string
    if (literal.equalsi("kb"))  return uint32_t(round(value * 1000));
    if (literal.equalsi("kib")) return uint32_t(round(value * 1024));
    if (literal.equalsi("mb"))  return uint32_t(round(value * 1000 * 1000));
    if (literal.equalsi("mib")) return uint32_t(round(value * 1024 * 1024));
    return uint32_t(ceil(value));
}

static std::string toLiteral(uint32_t bytes) noexcept
{
    char buffer[128];
    if      (bytes < 1000)    sprintf(buffer, "%dB", bytes);
    else if (bytes < 1000000) sprintf(buffer, "%.2fKB", double(bytes) / 1000.0);
    else                      sprintf(buffer, "%.2fMB", double(bytes) / 1000000.0);
    return buffer;
}

static std::string toRateLiteral(int bytesPerSec) noexcept
{
    return bytesPerSec > 0 ? toLiteral(bytesPerSec) + "/s" : "unlimited B/s";
}
