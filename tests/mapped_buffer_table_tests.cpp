// Regression tests for the constant-buffer map bookkeeping.
//
// The bug these lock down: records were appended unconditionally and only ever
// removed by a matching Unmap through our detour. A resource mapped twice
// without an intervening Unmap therefore appended a second record and orphaned
// the first, so slots drained away over a session; once all 256 were held,
// every subsequent Map was dropped and view injection stopped working with
// nothing in the log to explain it.

#include "mapped_buffer_table.h"

#include "test_support.h"

namespace {

using camera::MappedBufferTable;
using tests::Check;

// Stand-ins for ID3D11Resource* / the driver's mapped pointer. Only their
// identity matters to the table.
char g_resourceA, g_resourceB, g_resourceC;
char g_dataA, g_dataB, g_dataC;

void TestInsertThenTakeRoundTrips() {
    MappedBufferTable table;
    Check(table.Insert(&g_resourceA, &g_dataA, 576), "insert reports success");
    Check(table.Size() == 1, "insert grows the table");

    MappedBufferTable::Record rec{};
    Check(table.Take(&g_resourceA, rec), "take finds the record");
    Check(rec.data == &g_dataA && rec.size == 576u, "take returns the mapped pointer and size");
    Check(table.Size() == 0, "take removes the record");
}

void TestTakeOfUnknownResourceIsANoOp() {
    MappedBufferTable table;
    table.Insert(&g_resourceA, &g_dataA, 576);

    MappedBufferTable::Record rec{};
    Check(!table.Take(&g_resourceB, rec), "take of an unrecorded resource reports false");
    Check(table.Size() == 1, "a failed take leaves the table untouched");
}

// The leak itself: a second Map for a resource that never came back through
// Unmap must reuse its slot, not consume another one.
void TestRemappingReplacesRatherThanLeaks() {
    MappedBufferTable table;
    bool allAccepted = true;
    for (int i = 0; i < MappedBufferTable::kCapacity * 4; ++i)
        allAccepted = table.Insert(&g_resourceA, &g_dataA, 576) && allAccepted;
    Check(allAccepted, "re-mapping the same resource is never rejected");
    Check(table.Size() == 1, "re-mapping one resource never grows past a single slot");

    table.Insert(&g_resourceA, &g_dataB, 1024);
    MappedBufferTable::Record rec{};
    Check(table.Take(&g_resourceA, rec), "the replaced record is still takeable");
    Check(rec.data == &g_dataB && rec.size == 1024u, "re-mapping publishes the newest pointer");
}

void TestFullTableReportsInsteadOfSilentlyDropping() {
    MappedBufferTable table;
    // Distinct resources, so nothing can be replaced - the table genuinely fills.
    static char resources[MappedBufferTable::kCapacity];
    for (int i = 0; i < MappedBufferTable::kCapacity; ++i) {
        if (!table.Insert(&resources[i], &g_dataA, 576)) {
            Check(false, "filling to capacity should succeed");
            return;
        }
    }
    Check(table.Full(), "table reports itself full at capacity");
    Check(!table.Insert(&g_resourceC, &g_dataC, 576), "an overflowing insert reports failure");
    Check(table.Size() == MappedBufferTable::kCapacity, "a rejected insert does not grow the table");

    // A slot freeing up must let the next Map back in.
    MappedBufferTable::Record rec{};
    table.Take(&resources[0], rec);
    Check(table.Insert(&g_resourceC, &g_dataC, 576), "insert succeeds again once a slot frees");
}

// Take() compacts by moving the last record into the hole. Everything still
// recorded has to remain findable afterwards.
void TestTakeFromTheMiddleKeepsTheRest() {
    MappedBufferTable table;
    table.Insert(&g_resourceA, &g_dataA, 64);
    table.Insert(&g_resourceB, &g_dataB, 128);
    table.Insert(&g_resourceC, &g_dataC, 256);

    MappedBufferTable::Record rec{};
    Check(table.Take(&g_resourceB, rec) && rec.size == 128u, "middle record comes out intact");
    Check(table.Size() == 2, "removing one leaves two");
    Check(table.Take(&g_resourceA, rec) && rec.size == 64u, "first record survives compaction");
    Check(table.Take(&g_resourceC, rec) && rec.size == 256u, "last record survives compaction");
    Check(table.Size() == 0, "table drains");
}

}  // namespace

int RunMappedBufferTableTests() {
    tests::Begin("MappedBufferTable");
    TestInsertThenTakeRoundTrips();
    TestTakeOfUnknownResourceIsANoOp();
    TestRemappingReplacesRatherThanLeaks();
    TestFullTableReportsInsteadOfSilentlyDropping();
    TestTakeFromTheMiddleKeepsTheRest();
    return tests::g_failures;
}
