#ifndef CHAPEAU_UNITSTATE_H
#define CHAPEAU_UNITSTATE_H

// INTERNAL to Chapeau. Not part of the public interface: the public headers only
// forward-declare UnitState, no public signature mentions it, and nothing here is
// meant to be used by applications. (The header is installed with the others only
// because the library installs every header of src/.)
//
// A "unit" is one database connection (a Database), the result sets (cursors) it
// has produced that have not been destroyed, and the prepared statements they use.
// UnitState is the small object that the database and all of its cursors share:
//
//   * the unit's serial number: process-wide unique, assigned once;
//   * the CLAIM: a non-blocking, atomic "someone is inside this unit" marker, with
//     the key of the thread that holds it, so that a refusal made by the thread
//     that already holds the claim (re-entrance from a callback) can be told from
//     one made by another thread (overlap);
//   * the unit's aggregate count of refused calls;
//   * whether refusals are to be logged (set by the database's setLogsErrors()).
//
// As of H1a-12f NOTHING TAKES THE CLAIM: no database or cursor operation acquires
// it, nothing is ever refused, the counters stay at zero in ordinary use, and
// nothing about the behavior of the library changed. This is the infrastructure
// that the claims of a later milestone are built on; it is tested on its own.
//
// Ownership: the database and each cursor hold a std::shared_ptr<UnitState>. The
// state therefore lives as long as the longest-lived of them, in particular as long
// as a cursor that outlives its database, and no one holds a raw pointer to it.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace chapeau {

class Database;
class SQLiteResultSet;

/**
 * What the calling thread was last refused. One record per thread, in
 * thread-local storage that lives in UnitState.cpp (never in a header); trivially
 * destructible; it holds no pointer to a database or a cursor, only the serial
 * number of the unit, and a string literal (static lifetime) for the operation.
 */
struct RefusalRecord {
   std::uint64_t sequence;     // refusals this thread has experienced, over all units; 0 = none yet
   std::uint64_t unitSerial;   // the unit of the most recent one; 0 = none
   const char* operation;      // the operation of the most recent one (a string literal); null = none
};

class UnitState {
public:
   UnitState();
   ~UnitState();

   UnitState(const UnitState&) = delete;
   UnitState& operator=(const UnitState&) = delete;

   /**
    * The unit's serial number: unique in this process image, never 0, assigned
    * from one atomic counter when the state is created. It is not derived from an
    * address or a thread identifier, and it is never reused while the counter has
    * not wrapped. Theoretical wraparound: the counter has 64 bits; at one new
    * unit per nanosecond (far beyond what is possible) it would take about 584
    * years to wrap, after which numbers would start again from 1 and uniqueness
    * would no longer hold. 0 is skipped so that it can mean 'no unit'.
    */
   std::uint64_t serial() const { return m_serial; }

   // ---- the claim ----------------------------------------------------------------------
   // tryClaim(holder) either takes the claim for 'holder' (a key from
   // currentThreadKey()) and returns true, or returns false and changes nothing.
   // It never blocks and never records anything. release() must be called by the
   // holder, once, to give the claim back.
   bool tryClaim(std::uintptr_t holder);
   void release();
   // is the claim held by anyone? (the probe: reads, writes nothing, records nothing)
   bool isClaimed() const { return m_claimed.load(std::memory_order_acquire) != 0; }
   // is it held by the thread with this key?
   bool heldBy(std::uintptr_t holder) const;

   /**
    * A key that identifies the calling thread among the threads that are alive:
    * the address of a thread-local object. It is NOT a std::thread::id and is not
    * used to attribute anything: it is only compared with the holder of a claim
    * that is being held, by a thread that is alive, to tell 're-entrance by the
    * holder' from 'overlap by another thread'. (Once a thread has ended, another
    * thread may get the same key; a claim is never held by a thread that ended.)
    */
   static std::uintptr_t currentThreadKey();

   // ---- refusals -----------------------------------------------------------------------
   /**
    * Refused calls on this unit since it was created. A monotonically increasing
    * 64-bit counter, readable at any time without claiming the unit, and still
    * readable through a cursor that outlives its database. It is never reset.
    * Theoretical wraparound: 2^64 refusals (584 years at one per nanosecond); it
    * would then continue from 0.
    */
   std::uint64_t refusedCalls() const { return m_refusals.load(std::memory_order_relaxed); }

   /**
    * The cold path of a refused call: counts it on this unit and on the calling
    * thread, and logs it if logging is enabled and the count is a threshold (see
    * shouldLog). Returns the unit's new count. It does not touch the database's
    * error snapshot, takes no lock, and holds nothing while it prints. 'operation'
    * must be a string literal (it is kept in the thread's record).
    */
   std::uint64_t recordRefusal(const char* operation);

   // Logging. The database's setLogsErrors() mirrors its setting here, so that a
   // cursor can log after its database is gone. Default: off.
   void setLoggingEnabled(bool enabled) { m_logging.store(enabled, std::memory_order_relaxed); }
   bool loggingEnabled() const { return m_logging.load(std::memory_order_relaxed); }

   /**
    * Which refusals of a unit are logged: the first, and then every one whose
    * cumulative count is a power of two (1, 2, 4, 8, ...). A loop that is refused
    * a million times therefore writes about twenty lines.
    */
   static bool shouldLog(std::uint64_t count) { return count != 0 && (count & (count - 1)) == 0; }

   // ---- the calling thread's record ----------------------------------------------------
   // How many calls the calling thread has had refused, over all units (the number
   // that Database::refusedCallsOnThisThread() returns), and the whole record.
   static std::uint64_t refusedCallsOnThisThread();
   static RefusalRecord threadRecord();

private:
   const std::uint64_t m_serial;
   std::atomic<int> m_claimed;                    // 0 free, 1 held
   std::atomic<std::uintptr_t> m_holder;          // the holder's key while the claim is held, else 0
   std::atomic<std::uint64_t> m_refusals;
   std::atomic<bool> m_logging;
};

/**
 * A scoped claim on a unit. NOT USED by any database or cursor operation yet.
 *
 * Constructed with a unit it either holds the claim (ok()) or was refused. A
 * refusal is counted and logged through UnitState::recordRefusal() unless the
 * claim is a Probe. reentrant() says whether the refusal was made by the thread
 * that holds the claim. A null unit always succeeds and holds nothing.
 *
 * Probe: for questions such as inUse() ("is anyone inside?") that must not count
 * as a violation when the answer is 'yes': nothing is recorded, logged or counted.
 * A probe that is ok() has acquired the claim, and only then may the caller touch
 * the unit's state.
 */
class UnitClaim {
public:
   enum class Reporting { Report, Probe };

   UnitClaim(UnitState* unit, const char* operation, Reporting reporting = Reporting::Report);
   ~UnitClaim();

   UnitClaim(const UnitClaim&) = delete;
   UnitClaim& operator=(const UnitClaim&) = delete;

   bool ok() const { return m_ok; }
   bool reentrant() const { return m_reentrant; }

private:
   UnitState* m_unit;
   bool m_ok;
   bool m_held;
   bool m_reentrant;
};

/**
 * Access to the unit state of a database or a cursor for Chapeau's own tests and
 * internals. It is a friend of both classes so that the pointer need not be public.
 */
struct UnitStateAccess {
   static std::shared_ptr<UnitState> of(const Database& database);
   static std::shared_ptr<UnitState> of(const SQLiteResultSet& resultSet);
};

}

#endif
