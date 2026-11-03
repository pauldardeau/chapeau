#ifndef CHAPEAU_UNITSTATE_H
#define CHAPEAU_UNITSTATE_H

// INTERNAL to Chapeau. Not part of the public interface: the public headers only
// forward-declare UnitState, no public signature mentions it, and nothing here is
// meant to be used by applications. (It is not installed.)
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
// Since H1a-12g every public entry point of SQLiteDatabase, of Database's statement
// cache and of SQLiteResultSet takes the claim for the duration of the call (see
// UnitClaim), and a call that does not get it is REFUSED: it touches nothing of the
// unit, returns a neutral value, and is counted and (if logging is on) logged. The
// destructors of a database and of a cursor cannot refuse; if the unit is claimed
// they report it on stderr and abort the process.
//
// Ownership: the database and each cursor hold a std::shared_ptr<UnitState>. The
// state therefore lives as long as the longest-lived of them, in particular as long
// as a cursor that outlives its database, and no one holds a raw pointer to it
// beyond the extent of a call that is made on one of those owners.

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
   //
   // Two atomics: m_claimed (0 free, 1 held) and m_holder (the key of the thread that
   // holds it, else 0). The operations and their memory orders, exactly:
   //
   //    tryClaim(k)  m_claimed.compare_exchange_strong(0 -> 1, success acq_rel, failure relaxed);
   //                 then, if it succeeded, m_holder.store(k, relaxed)
   //    release()    m_holder.store(0, relaxed); then m_claimed.store(0, release)
   //    heldBy(k)    m_claimed.load(acquire) != 0, then m_holder.load(relaxed) == k
   //    isClaimed()  m_claimed.load(acquire) != 0
   //
   // What follows is the C++ memory model's guarantee. Nothing in it depends on the
   // hardware (x86's strong ordering included), and the places where it rests on an
   // assumption about the implementation are marked 'assumption'.
   //
   //  1. Mutual exclusion and hand-off. A compare-exchange is a read-modify-write, and a
   //     read-modify-write reads the last value in the modification order of m_claimed.
   //     The only writers of m_claimed are successful compare-exchanges (0 -> 1) and
   //     releases (-> 0), so they alternate in that order; a compare-exchange succeeds
   //     only if the value before it is 0, hence only after the release (or the
   //     constructor) that wrote it, and at most one of two competing ones does. That
   //     release store synchronizes with the acquire half of the compare-exchange that
   //     reads it: every write the previous holder made while it held the claim
   //     happens-before everything the new holder does. This is the hand-off.
   //
   //  2. A thread never reads its own key from m_holder unless it holds the claim. Let T
   //     have the key K. T stores K after its successful compare-exchange and 0 before
   //     its release, in that order. By the coherence rules for one atomic object:
   //       - T's store of K precedes T's store of 0 in the modification order of m_holder
   //         (write-write); and a load of T's after its store of 0 reads that 0 or a later
   //         value of m_holder (write-read), never the K before it (read-read).
   //       - The later values are stores by other threads: their own keys, or 0. No other
   //         live thread has the key K (assumption, see 5a), so none stores K. T itself
   //         stores K again only when it takes the claim again.
   //       - While T holds the claim, T's own load reads K: no other thread can store to
   //         m_holder between T's K and T's 0, because a thread stores to it only after a
   //         successful compare-exchange, which needs the 0 that T's release writes, and
   //         T's 0 store is sequenced before that release (item 1).
   //     So heldBy(K), asked by T, is true exactly while T holds the claim. That is what
   //     tells re-entrance (by the holder) from overlap (by any other thread). It needs
   //     no ordering on m_holder (relaxed is enough) and none from m_claimed; the acquire
   //     load of m_claimed in heldBy() is not needed for this and is kept for isClaimed().
   //
   //  3. The window between a thread's successful compare-exchange and its store to
   //     m_holder (claimed, holder 0), and a release that has cleared the holder but not
   //     yet the claim (claimed, holder 0 again), are harmless: no thread matches 0 (it is
   //     not a key), so a refused thread is classified 'overlap'. That is right: the
   //     thread that is in the window holds the claim, and it is not the refused one,
   //     which is running its own call. The order of the two stores in release() matters:
   //     if the claim were given back first, the next holder's key could be stored and
   //     then overwritten with 0 by the late store of the releaser, and that holder would
   //     no longer recognize its own nested call.
   //
   //  4. A refused caller's compare-exchange failed: it is a load, and may read a 1 that is
   //     not the latest value (the 'last value' rule is for the read-modify-write that
   //     succeeds). That can only make a refusal that is already due, or, without any
   //     synchronization with the previous holder's release (outside the contract), a
   //     spurious one. It cannot misclassify it: classification is item 2, which does not
   //     depend on which 1 was read. With the hand-off synchronization the contract asks
   //     for, the release happens-before the next call, and coherence then makes the load
   //     read that release or a later write, so a hand-off is never refused.
   //
   //  5. Assumptions about the implementation:
   //     a. The key is the address of a thread_local object (see currentThreadKey()).
   //        The objects of two threads that are alive at the same time are different
   //        objects, so their addresses differ. A thread that has ended may leave its
   //        address to a thread created later; that matters only if the ended thread
   //        ended while holding a claim, which a normal return or an exception cannot
   //        do (the claim is released by a destructor) and only a thread cancellation or
   //        a longjmp across the frame could.
   //     b. std::atomic<int> and std::atomic<std::uintptr_t> are lock-free on the
   //        platforms Chapeau supports (checked by a test), so that these operations
   //        are not secretly guarded by a lock.
   //
   // tryClaim(holder) either takes the claim for 'holder' (a key from
   // currentThreadKey(), never 0) and returns true, or returns false and changes
   // nothing. It never blocks and never records anything. release() must be called by
   // the holder, once, to give the claim back.
   bool tryClaim(std::uintptr_t holder) {
      int expected = 0;
      if (!m_claimed.compare_exchange_strong(expected, 1,
                                             std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
         return false;
      }
      m_holder.store(holder, std::memory_order_relaxed);
      return true;
   }

   void release() {
      // the holder first, then the claim: whoever takes the claim next sets its own
      m_holder.store(0, std::memory_order_relaxed);
      m_claimed.store(0, std::memory_order_release);
   }

   // is the claim held by anyone? (the probe: reads, writes nothing, records nothing)
   bool isClaimed() const { return m_claimed.load(std::memory_order_acquire) != 0; }
   // is it held by the thread with this key?
   bool heldBy(std::uintptr_t holder) const {
      return holder != 0 &&
             m_claimed.load(std::memory_order_acquire) != 0 &&
             m_holder.load(std::memory_order_relaxed) == holder;
   }

   /**
    * A key that identifies the calling thread among the threads that are alive:
    * the address of a thread-local object. It is NOT a std::thread::id and is not
    * used to attribute anything: it is only compared with the holder of a claim
    * that is being held, by a thread that is alive, to tell 're-entrance by the
    * holder' from 'overlap by another thread'. (Once a thread has ended, another
    * thread may get the same key; a claim is never held by a thread that ended.)
    * Defined out of line, in UnitState.cpp, so that the thread-local lives in the
    * library and not in each translation unit that includes this header.
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
 * A scoped claim on a unit.
 *
 * Constructed with a unit it either holds the claim (ok()) or was refused. A
 * refusal is counted and logged through UnitState::recordRefusal() unless the
 * claim is a Probe. reentrant() says whether the refusal was made by the thread
 * that holds the claim. A null unit always succeeds and holds nothing.
 *
 * The success path is inline (one compare-exchange and a thread-key lookup); every
 * refusal goes through the out-of-line refused(), so the hot paths stay small.
 *
 * The claim is released when the object is destroyed. The unit must outlive the
 * claim: a UnitClaim is always a local of a member function of an object that holds
 * the unit's shared_ptr, so it does.
 *
 * Probe: for questions such as inUse() ("is anyone inside?") that must not count
 * as a violation when the answer is 'yes': nothing is recorded, logged or counted.
 * A probe that is ok() has acquired the claim, and only then may the caller touch
 * the unit's state.
 *
 * Destructors cannot refuse. A destructor takes a Probe claim and calls
 * abortUnlessOk(): if the unit is claimed it writes one line to stderr and aborts
 * the process. That is a misuse detector for destroying an object while another call
 * is using its unit, not a guarantee of safety: if the destruction itself races with
 * the other call, the claim check and the destruction are not atomic with each other
 * (the object may already be partly gone when the other thread looks).
 */
class UnitClaim {
public:
   enum class Reporting { Report, Probe };

   UnitClaim(UnitState* unit, const char* operation, Reporting reporting = Reporting::Report)
      : m_unit(unit), m_ok(true), m_held(false), m_reentrant(false) {
      if (unit == nullptr) {
         return;
      }
      const std::uintptr_t me = UnitState::currentThreadKey();
      if (unit->tryClaim(me)) {
         m_held = true;
         return;
      }
      refused(me, operation, reporting);
   }

   ~UnitClaim() {
      if (m_held) {
         m_unit->release();
      }
   }

   UnitClaim(const UnitClaim&) = delete;
   UnitClaim& operator=(const UnitClaim&) = delete;

   bool ok() const { return m_ok; }
   bool reentrant() const { return m_reentrant; }

   /**
    * For a destructor: does nothing if the claim is held; otherwise prints
    * "Chapeau: fatal: <who> ran while its unit (<serial>) was in use by another
    * call ..." on stderr and calls std::abort(). Never returns in that case.
    */
   void abortUnlessOk(const char* who) const {
      if (!m_ok) {
         abortBusy(m_unit, who, m_reentrant);
      }
   }

private:
   void refused(std::uintptr_t me, const char* operation, Reporting reporting);
   [[noreturn]] static void abortBusy(const UnitState* unit, const char* who, bool sameThread);

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
