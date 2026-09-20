#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <linux/perf_event.h>
#include <queue>
#include <vector>

namespace pebs_ordering {

struct sample {
    uint64_t ip = 0, time = 0, addr = 0, sequence = 0;
    uint32_t pid = 0, tid = 0;
    uint16_t cpu = 0;
    uint8_t type = 0, reason = 0;
};

// Copy a record only from the committed [tail, head) region, including wrap.
inline bool copy_ring(const char *ring, uint64_t size, uint64_t head, uint64_t tail,
                      void *destination, uint64_t bytes)
{
    if (size == 0 || head < tail || head - tail > size || bytes > head - tail || bytes > size)
        return false;
    const uint64_t offset = tail % size;
    const uint64_t first = std::min(bytes, size - offset);
    std::memcpy(destination, ring + offset, first);
    std::memcpy(static_cast<char *>(destination) + first, ring, bytes - first);
    return true;
}

// Kernel sample layout for the enabled fields: IP, TID, TIME, ADDR.
inline bool decode_sample(const void *record, size_t bytes, bool timestamped, sample &out)
{
    const size_t expected = sizeof(perf_event_header) + 8 + 8 + (timestamped ? 8 : 0) + 8;
    if (bytes < expected)
        return false;
    perf_event_header header{};
    std::memcpy(&header, record, sizeof(header));
    if (header.type != PERF_RECORD_SAMPLE || header.size != bytes || bytes != expected)
        return false;
    const char *cursor = static_cast<const char *>(record) + sizeof(header);
    std::memcpy(&out.ip, cursor, 8); cursor += 8;
    std::memcpy(&out.pid, cursor, 4); cursor += 4;
    std::memcpy(&out.tid, cursor, 4); cursor += 4;
    out.time = 0;
    if (timestamped) { std::memcpy(&out.time, cursor, 8); cursor += 8; }
    std::memcpy(&out.addr, cursor, 8);
    return !timestamped || out.time != 0;
}

struct later {
    bool operator()(const sample &a, const sample &b) const
    {
        return a.time != b.time ? a.time > b.time : a.sequence > b.sequence;
    }
};

class merger {
    std::priority_queue<sample, std::vector<sample>, later> pending;
    uint64_t last_time = 0;
public:
    uint64_t late_samples = 0;
    size_t size() const { return pending.size(); }
    void push(const sample &value) { pending.push(value); }
    template<class Consumer> void release(uint64_t watermark, Consumer consume)
    {
        while (!pending.empty() && pending.top().time <= watermark) {
            sample value = pending.top();
            pending.pop();
            // Never silently rewrite history or feed an out-of-order sample.
            // Any late sample invalidates the experiment in the sidecar.
            if (value.time < last_time) { ++late_samples; continue; }
            last_time = value.time;
            consume(value);
        }
    }
};
} // namespace pebs_ordering
