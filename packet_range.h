#pragma once
#include "logging.h"
#include "utils.h"
#include <vector>

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

        struct Missing { int32_t count, first, last; };
        std::vector<Missing> missing;
        int32_t totalMissing = 0;

        int32_t lastKnownId = ids[0];
        for (size_t i = 1; i < ids.size(); ++i)
        {
            int32_t expectedId = lastKnownId + 1;
            int32_t actualId = ids[i]; // on error, actualId > expectedId
            if (actualId != expectedId)
            {
                int32_t numMissing = (actualId - expectedId);
                totalMissing += numMissing;
                if (numMissing == 1u)
                    missing.push_back({ 1, expectedId, expectedId });
                else
                    missing.push_back({ numMissing, expectedId, actualId - 1 });
            }
            lastKnownId = actualId;
        }

        int numSegments = (int)missing.size();
        if (numSegments > 0)
        {
            LogInfo(ORANGE("WARNING: Missing total:%d  segments:%d"), totalMissing, numSegments);
            if (numSegments > 20)
            {
                LogInfo(CYAN("WARNING: Too many missing segments to list, printing first 20"));
                numSegments = 20;
            }
            for (int i = 0; i < numSegments; ++i)
            {
                const Missing& m = missing[i];
                if (m.count == 1) LogInfo(ORANGE("WARNING: Missing 1 seqid %d"), m.first);
                else              LogInfo(ORANGE("WARNING: Missing %d seqid %d .. %d"), m.count, m.first, m.last);
            }
        }
    }
};

