#pragma once

// Bookkeeping for the constant buffers that are currently mapped. Map() records
// the CPU pointer the driver handed out; Unmap() looks it back up so the buffer
// can be rewritten on its way to the GPU.
//
// A record only clears when the matching Unmap arrives through our detour, so
// anything mapped and then released - or unmapped after the hook is gone - would
// hold its slot for the rest of the session. Slots are finite, and once they are
// all held every subsequent Map is dropped and view injection stops with nothing
// in the log to say why. Two rules keep that from happening silently: re-mapping
// a resource REPLACES its record rather than appending a second one (a duplicate
// is what leaked a slot per buffer per level load), and a full table is reported
// rather than absorbed.

namespace camera {

class MappedBufferTable {
public:
    static constexpr int kCapacity = 256;

    struct Record {
        const void* resource;
        void* data;
        unsigned size;
    };

    // Replaces any existing record for `resource`. Returns false when the table
    // is full and the record had to be dropped.
    bool Insert(const void* resource, void* data, unsigned size) {
        const int existing = Find(resource);
        if (existing >= 0) {
            m_records[existing].data = data;
            m_records[existing].size = size;
            return true;
        }
        if (m_count >= kCapacity) return false;
        m_records[m_count].resource = resource;
        m_records[m_count].data = data;
        m_records[m_count].size = size;
        ++m_count;
        return true;
    }

    // Removes the record for `resource`, copying it to `out`. Returns false when
    // the resource is not currently recorded.
    bool Take(const void* resource, Record& out) {
        const int index = Find(resource);
        if (index < 0) return false;
        out = m_records[index];
        m_records[index] = m_records[--m_count];
        return true;
    }

    int Size() const { return m_count; }
    bool Full() const { return m_count >= kCapacity; }

private:
    int Find(const void* resource) const {
        for (int i = 0; i < m_count; ++i)
            if (m_records[i].resource == resource) return i;
        return -1;
    }

    Record m_records[kCapacity] = {};
    int m_count = 0;
};

}  // namespace camera
