// UnitState.cpp
//
// Everything with static or thread-local storage in the unit-state infrastructure
// lives here, out of line: the process-wide serial counter and the per-thread
// refusal record. Nothing of that is defined in a header, so there is exactly one
// of each per copy of the library, however many translation units use it.

#include "UnitState.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Database.h"
#include "SQLiteResultSet.h"

using namespace chapeau;

namespace {

// The next serial number to hand out minus one. Constant-initialized, so it is
// usable before any dynamic initialization, in any order.
std::atomic<std::uint64_t> g_lastSerial(0);

// The calling thread's record. Constant-initialized and trivially destructible, so
// access needs no guard variable and no destructor is registered for the thread.
thread_local RefusalRecord t_refusalRecord = {0, 0, nullptr};

// Only its address is used: a key for the calling thread among the live threads.
thread_local char t_threadKeyAnchor = 0;

std::uint64_t nextSerial() {
   std::uint64_t serial = 0;
   do {
      // fetch_add is atomic: no two callers get the same value. 0 is skipped (it
      // only matters after the counter has wrapped around) because 0 means 'no unit'.
      serial = g_lastSerial.fetch_add(1, std::memory_order_relaxed) + 1;
   } while (serial == 0);
   return serial;
}

}

//******************************************************************************

UnitState::UnitState() :
   m_serial(nextSerial()),
   m_claimed(0),
   m_holder(0),
   m_refusals(0),
   m_logging(false) {
}

//******************************************************************************

UnitState::~UnitState() {
}

//******************************************************************************

std::uintptr_t UnitState::currentThreadKey() {
   return reinterpret_cast<std::uintptr_t>(&t_threadKeyAnchor);
}

//******************************************************************************

std::uint64_t UnitState::recordRefusal(const char* operation) {
   // the unit's count first: it is what the log line reports
   const std::uint64_t count = m_refusals.fetch_add(1, std::memory_order_relaxed) + 1;

   // the calling thread's own record; no other thread ever touches it
   ++t_refusalRecord.sequence;
   t_refusalRecord.unitSerial = m_serial;
   t_refusalRecord.operation = operation;

   // Logging, last, with nothing held: no lock, no shared buffer. The line is built
   // in a local string and written with one printf, as fail() does. It never reaches
   // the database's error snapshot: this object does not know it.
   if (m_logging.load(std::memory_order_relaxed) && shouldLog(count)) {
      const std::string line =
         "DB Warning [ConcurrentUse]: a call was refused because the unit is in use by another call"
         " (unit " + std::to_string(m_serial) +
         ", " + (operation != nullptr ? operation : "?") +
         ", refusal " + std::to_string(count) + " on this unit)";
      ::printf("%s\n", line.c_str());
   }

   return count;
}

//******************************************************************************

std::uint64_t UnitState::refusedCallsOnThisThread() {
   return t_refusalRecord.sequence;
}

//******************************************************************************

RefusalRecord UnitState::threadRecord() {
   return t_refusalRecord;
}

//******************************************************************************
//******************************************************************************

void UnitClaim::refused(std::uintptr_t me, const char* operation, Reporting reporting) {
   m_ok = false;
   m_reentrant = m_unit->heldBy(me);

   if (reporting == Reporting::Report) {
      m_unit->recordRefusal(operation);
   }
}

//******************************************************************************

void UnitClaim::abortBusy(const UnitState* unit, const char* who, bool sameThread) {
   // one line, one fprintf: nothing shared, nothing allocated
   std::fprintf(stderr,
                "Chapeau: fatal: %s ran while its unit (%llu) was in use by %s; "
                "a database and its result sets must be idle when they are destroyed. Aborting.\n",
                who,
                static_cast<unsigned long long>(unit != nullptr ? unit->serial() : 0),
                sameThread ? "an earlier call on this same thread" : "a call on another thread");
   std::fflush(stderr);
   std::abort();
}

//******************************************************************************
//******************************************************************************

std::shared_ptr<UnitState> UnitStateAccess::of(const Database& database) {
   return database.m_unit;
}

//******************************************************************************

std::shared_ptr<UnitState> UnitStateAccess::of(const SQLiteResultSet& resultSet) {
   return resultSet.m_unit;
}
