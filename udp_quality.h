#pragma once
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h> // errno
#include <stdarg.h> // va_start
#include <math.h> // round
#include <vector>
#include <rpp/timer.h>
#include <rpp/strview.h>

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

static std::string toRateLiteral(int bytesPerSec) const noexcept
{
    return bytesPerSec > 0 ? toLiteral(bytesPerSec) + "/s" : "unlimited B/s";
}

struct PacketRange
{
    std::vector<int32_t> ids;

    int size() const noexcept { return (int)ids.size(); }
    void reset() noexcept { ids.clear(); }
    void push(int32_t id) noexcept { ids.push_back(id); }
    void printErrors() noexcept
    {
        if (ids.empty())
            return;
        int32_t lastKnownId = ids[0];
        for (size_t i = 1; i < ids.size(); ++i)
        {
            int32_t expectedId = lastKnownId + 1;
            int32_t actualId = ids[i]; // on error, actualId > expectedId
            if (actualId != expectedId)
            {
                int32_t numMissing = (actualId - expectedId);
                if (numMissing == 1u)
                    printf(ORANGE("WARNING: Missing packets 1 seqid %d\n"), expectedId);
                else
                    printf(ORANGE("WARNING: Missing packets %d seqid %d .. %d\n"), numMissing, expectedId, actualId - 1);
            }
            lastKnownId = actualId;
        }
    }
};

