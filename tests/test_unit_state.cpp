#include "test_unit_state.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "Database.h"
#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "UnitState.h"

using namespace std;
using namespace chapeau;


namespace {

// what the library writes to stdout while body() runs. Only where dup2 exists;
// elsewhere 'captured' is false and the tests skip what they would check in it.
string captureStdout(const function<void()>& body, bool& captured) {
#ifndef _WIN32
   fflush(stdout);
   FILE* file = tmpfile();
   const int saved = ::dup(1);
   if (file == nullptr || saved < 0) {
      captured = false;
      body();
      return string();
   }
   ::dup2(::fileno(file), 1);
   body();
   fflush(stdout);
   ::dup2(saved, 1);
   ::close(saved);

   string out;
   rewind(file);
   char buffer[512];
   size_t got = 0;
   while ((got = fread(buffer, 1, sizeof buffer, file)) > 0) {
      out.append(buffer, got);
   }
   fclose(file);
   captured = true;
   return out;
#else
   captured = false;
   body();
   return string();
#endif
}

size_t lineCount(const string& text) {
   return static_cast<size_t>(count(text.begin(), text.end(), '\n'));
}

SQLiteResultSet* openCursor(SQLiteDatabase& db, const string& sql) {
   return dynamic_cast<SQLiteResultSet*>(db.executeQuery(sql));
}

// one-shot event between two test threads (waits are bounded)
class Event {
public:
   void set() {
      lock_guard<mutex> lock(m_mutex);
      m_set = true;
      m_cv.notify_all();
   }
   bool wait() {
      unique_lock<mutex> lock(m_mutex);
      return m_cv.wait_until(lock, chrono::system_clock::now() + chrono::seconds(20), [this] { return m_set; });
   }
private:
   mutex m_mutex;
   condition_variable m_cv;
   bool m_set = false;
};

}


TestUnitState::TestUnitState() :
   poivre::TestSuite("TestUnitState") {
}

void TestUnitState::runTests() {
   test_database_and_cursors_share_one_unit_state();
   test_cursor_keeps_the_unit_state_after_the_database_is_destroyed();
   test_independent_databases_have_different_serials();
   test_serial_assignment_under_concurrent_construction();
   test_claim_has_one_owner_and_refuses_the_rest();
   test_nested_claim_is_told_from_overlap();
   test_released_claim_allows_a_hand_off();
   test_probe_reports_nothing();
   test_thread_records_belong_to_their_threads();
   test_thread_identifier_reuse_does_not_mix_records();
   test_unit_counter_aggregates_over_threads();
   test_logging_thresholds();
   test_logging_disabled_is_silent();
   test_logging_follows_setLogsErrors();
   test_refusals_leave_the_error_snapshot_alone();
   test_public_accessors_are_zero_in_ordinary_use();
   test_public_accessors_report_refusals_when_there_are_some();
   test_claim_publishes_the_holder_after_it_takes_the_claim();
   test_claim_serializes_plain_data_under_contention();
   test_rapid_hand_off_between_two_threads();
   test_holder_is_classified_in_every_state_of_a_claim();
   test_hand_off_interleavings_are_classified_correctly();
   test_thread_keys_are_distinct_and_stable();
   test_three_threads_hand_off_in_a_fixed_order();
   test_three_party_rotation_never_shows_a_stale_key();
}

void TestUnitState::test_database_and_cursors_share_one_unit_state() {
   TEST_CASE("test_database_and_cursors_share_one_unit_state");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open(), "open");

   const shared_ptr<UnitState> unit = UnitStateAccess::of(db);
   require(unit != nullptr, "a database has a unit state");

   unique_ptr<SQLiteResultSet> first(openCursor(db, "select 1"));
   unique_ptr<SQLiteResultSet> second(openCursor(db, "select 2"));
   require(first && second, "two cursors");
   require(UnitStateAccess::of(*first).get() == unit.get(), "the first cursor shares the database's unit state");
   require(UnitStateAccess::of(*second).get() == unit.get(), "so does the second");
   require(unit.use_count() >= 4, "the database, the test and both cursors hold it (use count " + to_string(unit.use_count()) + ")");

   // a second database has a unit of its own, and so do its cursors
   SQLiteDatabase other(":memory:");
   require(other.open(), "a second database");
   unique_ptr<SQLiteResultSet> otherCursor(openCursor(other, "select 3"));
   require(otherCursor && UnitStateAccess::of(*otherCursor).get() == UnitStateAccess::of(other).get(), "its cursor shares its state");
   require(UnitStateAccess::of(other).get() != unit.get(), "and that is not the first database's");

   // the state does not change when the database is closed and opened again
   db.close();
   require(db.open(), "reopen");
   require(UnitStateAccess::of(db).get() == unit.get(), "close() and open() keep the same unit state");
}

void TestUnitState::test_cursor_keeps_the_unit_state_after_the_database_is_destroyed() {
   TEST_CASE("test_cursor_keeps_the_unit_state_after_the_database_is_destroyed");
   unique_ptr<SQLiteDatabase> db(new SQLiteDatabase(":memory:"));
   db->setLogsErrors(false);
   require(db->open(), "open");
   unique_ptr<SQLiteResultSet> cursor(openCursor(*db, "select 1"));
   require(cursor != nullptr, "a cursor");

   weak_ptr<UnitState> watch = UnitStateAccess::of(*db);
   const uint64_t serial = UnitStateAccess::of(*db)->serial();
   require(!watch.expired(), "alive while the database is");

   db.reset();   // the database is destroyed; the cursor is not
   require(cursor->status() == DBResultSetStatus::Invalidated, "the cursor was invalidated");
   require(!watch.expired(), "the unit state is still alive: the cursor holds it");

   shared_ptr<UnitState> kept = UnitStateAccess::of(*cursor);
   require(kept != nullptr && kept->serial() == serial, "the cursor still has the same unit state");
   requireStringEquals("0", to_string(kept->refusedCalls()), "its counter is readable and is 0");
   kept->recordRefusal("simulated");
   requireStringEquals("1", to_string(kept->refusedCalls()), "and still counts, through the cursor");

   // the cursor can be used and destroyed normally
   require(!cursor->next(), "next() on the invalidated cursor");
   cursor.reset();
   require(!watch.expired(), "still alive while this test holds it");
   kept.reset();
   require(watch.expired(), "gone once the last holder lets go: nothing leaked, nothing dangling");
}

void TestUnitState::test_independent_databases_have_different_serials() {
   TEST_CASE("test_independent_databases_have_different_serials");
   SQLiteDatabase a(":memory:");
   SQLiteDatabase b(":memory:");
   SQLiteDatabase c(":memory:");
   const uint64_t sa = UnitStateAccess::of(a)->serial();
   const uint64_t sb = UnitStateAccess::of(b)->serial();
   const uint64_t sc = UnitStateAccess::of(c)->serial();
   require(sa != 0 && sb != 0 && sc != 0, "no serial is 0");
   require(sa != sb && sb != sc && sa != sc, "all different");
   require(sa < sb && sb < sc, "assigned in increasing order within a thread");

   // a database that is closed and opened again keeps its serial; a cursor has its database's
   require(a.open(), "open");
   unique_ptr<SQLiteResultSet> cursor(openCursor(a, "select 1"));
   require(cursor && UnitStateAccess::of(*cursor)->serial() == sa, "a cursor reports its database's serial");

   // the serial is a counter, not an address
   require(sa != reinterpret_cast<uintptr_t>(UnitStateAccess::of(a).get()), "not the address of the state");
   require(sa != reinterpret_cast<uintptr_t>(&a), "not the address of the database");
}

void TestUnitState::test_serial_assignment_under_concurrent_construction() {
   TEST_CASE("test_serial_assignment_under_concurrent_construction");
   const int THREADS = 8;
   const int PER_THREAD = 500;
   vector<vector<uint64_t>> serials(THREADS);
   atomic<int> ready(0);
   atomic<bool> go(false);
   vector<thread> threads;
   for (int t = 0; t < THREADS; ++t) {
      threads.emplace_back([&, t] {
         ++ready;
         while (!go.load()) {
            this_thread::yield();
         }
         for (int i = 0; i < PER_THREAD; ++i) {
            UnitState state;   // a unit state per construction, as every database makes one
            serials[t].push_back(state.serial());
         }
      });
   }
   while (ready.load() < THREADS) {
      this_thread::yield();
   }
   go = true;
   for (thread& th : threads) {
      th.join();
   }

   vector<uint64_t> all;
   for (const auto& v : serials) {
      all.insert(all.end(), v.begin(), v.end());
   }
   requireStringEquals(to_string(THREADS * PER_THREAD), to_string(all.size()), "every construction got a serial");
   require(find(all.begin(), all.end(), 0u) == all.end(), "none is 0");
   sort(all.begin(), all.end());
   require(adjacent_find(all.begin(), all.end()) == all.end(), "all " + to_string(all.size()) + " serials are different");

   // within one thread they increase
   bool increasing = true;
   for (const auto& v : serials) {
      increasing = increasing && is_sorted(v.begin(), v.end()) && adjacent_find(v.begin(), v.end()) == v.end();
   }
   require(increasing, "and each thread's serials are strictly increasing");
}

void TestUnitState::test_claim_has_one_owner_and_refuses_the_rest() {
   TEST_CASE("test_claim_has_one_owner_and_refuses_the_rest");
   UnitState unit;
   require(!unit.isClaimed(), "free to begin with");
   const uint64_t before = UnitState::refusedCallsOnThisThread();

   {
      UnitClaim owner(&unit, "first");
      require(owner.ok(), "the first claim is granted");
      require(unit.isClaimed(), "the unit is claimed");
      require(unit.heldBy(UnitState::currentThreadKey()), "by this thread");

      // another thread is refused; it is not the holder
      bool ok = true, reentrant = true;
      uint64_t sequence = 99;
      thread other([&] {
         UnitClaim competitor(&unit, "competitor");
         ok = competitor.ok();
         reentrant = competitor.reentrant();
         sequence = UnitState::refusedCallsOnThisThread();
      });
      other.join();
      require(!ok, "a competing claim from another thread is refused");
      require(!reentrant, "and is not reported as re-entrance");
      requireStringEquals("1", to_string(sequence), "it counted for that thread");
      requireStringEquals("1", to_string(unit.refusedCalls()), "and for the unit");
      requireStringEquals("0", to_string(UnitState::refusedCallsOnThisThread() - before), "but not for the holder's thread");
      require(unit.isClaimed(), "the refusal did not disturb the owner's claim");
   }

   require(!unit.isClaimed(), "released when the owner's scope ends");
   {
      UnitClaim again(&unit, "again");
      require(again.ok(), "and then granted again");
   }
   requireStringEquals("1", to_string(unit.refusedCalls()), "a granted claim counts nothing");

   // a null unit is always granted and holds nothing
   UnitClaim none(nullptr, "none");
   require(none.ok() && !none.reentrant(), "a claim on no unit is granted");
}

void TestUnitState::test_nested_claim_is_told_from_overlap() {
   TEST_CASE("test_nested_claim_is_told_from_overlap");
   UnitState unit;
   UnitClaim outer(&unit, "outer");
   require(outer.ok(), "the outer claim");

   {
      UnitClaim nested(&unit, "nested");
      require(!nested.ok(), "a second claim by the holder is refused");
      require(nested.reentrant(), "and is identified as re-entrance by the holder");
   }
   require(unit.isClaimed() && unit.heldBy(UnitState::currentThreadKey()), "the refused nested claim did not release the outer one");

   bool otherHeld = true, otherReentrant = true;
   thread other([&] {
      otherHeld = unit.heldBy(UnitState::currentThreadKey());
      UnitClaim competitor(&unit, "overlap");
      otherReentrant = competitor.reentrant();
   });
   other.join();
   require(!otherHeld, "the other thread is not the holder");
   require(!otherReentrant, "its refusal is overlap, not re-entrance");

   // thread keys: stable within a thread, different between live threads
   const uintptr_t mine = UnitState::currentThreadKey();
   require(mine != 0 && mine == UnitState::currentThreadKey(), "a thread's key is stable");
   uintptr_t theirs = 0;
   Event stop;
   Event started;
   thread peer([&] {
      theirs = UnitState::currentThreadKey();
      started.set();
      stop.wait();
   });
   started.wait();
   require(theirs != 0 && theirs != mine, "two live threads have different keys");
   stop.set();
   peer.join();
   requireStringEquals("2", to_string(unit.refusedCalls()), "both refusals were counted on the unit, whatever their kind");
}

void TestUnitState::test_released_claim_allows_a_hand_off() {
   TEST_CASE("test_released_claim_allows_a_hand_off");
   UnitState unit;

   // A uses the unit and lets go; B (waiting for exactly that) takes it, and gives it back to the test thread
   Event aDone, bDone;
   bool aGot = false, bGot = false;
   int shared = 0;   // plain data handed over with the unit; ordered by the events, not by the claim
   thread a([&] {
      UnitClaim claim(&unit, "A");
      aGot = claim.ok();
      shared = 1;
   });
   a.join();
   aDone.set();
   thread b([&] {
      aDone.wait();
      UnitClaim claim(&unit, "B");
      bGot = claim.ok();
      shared += 10;
      bDone.set();
   });
   bDone.wait();
   b.join();
   require(aGot && bGot, "both threads were granted the claim in turn");
   requireStringEquals("11", to_string(shared), "the data went through both");

   {
      UnitClaim last(&unit, "test");
      require(last.ok(), "and so is the test thread, afterwards");
   }
   requireStringEquals("0", to_string(unit.refusedCalls()), "nothing was refused in a proper hand-off");

   // ping-pong: two threads take turns 200 times; every claim is granted
   mutex turnMutex;
   condition_variable turnChanged;
   int turn = 0;
   int refused = 0;
   int total = 0;
   auto player = [&](int me) {
      for (int i = 0; i < 100; ++i) {
         unique_lock<mutex> lock(turnMutex);
         turnChanged.wait_until(lock, chrono::system_clock::now() + chrono::seconds(20), [&] { return turn == me; });
         lock.unlock();
         {
            UnitClaim claim(&unit, "turn");
            if (claim.ok()) {
               ++total;
            } else {
               ++refused;
            }
         }
         lock.lock();
         turn = 1 - me;
         turnChanged.notify_all();
      }
   };
   thread p0([&] { player(0); });
   thread p1([&] { player(1); });
   p0.join();
   p1.join();
   requireStringEquals("200", to_string(total), "all 200 turns were granted");
   requireStringEquals("0", to_string(refused), "none was refused");
}

void TestUnitState::test_probe_reports_nothing() {
   TEST_CASE("test_probe_reports_nothing");
   UnitState unit;
   bool captured = false;
   unit.setLoggingEnabled(true);   // even with logging on, a probe is silent
   const uint64_t sequenceBefore = UnitState::refusedCallsOnThisThread();

   string output = captureStdout([&] {
      // free: the probe acquires the claim, and releases it again
      {
         UnitClaim probe(&unit, "probe", UnitClaim::Reporting::Probe);
         require(probe.ok(), "a probe of a free unit acquires the claim");
         require(unit.isClaimed(), "which is then held");
      }
      require(!unit.isClaimed(), "and released at the end of the probe's scope");

      // busy because another thread holds it
      Event held, finish;
      bool busy = false, reentrant = true;
      uint64_t otherSequence = 99;
      thread holder([&] {
         UnitClaim claim(&unit, "holder");
         held.set();
         finish.wait();
      });
      held.wait();
      thread prober([&] {
         UnitClaim probe(&unit, "probe", UnitClaim::Reporting::Probe);
         busy = !probe.ok();
         reentrant = probe.reentrant();
         otherSequence = UnitState::refusedCallsOnThisThread();
      });
      prober.join();
      finish.set();
      holder.join();
      require(busy, "a probe of a unit held by another thread says 'busy'");
      require(!reentrant, "and it is not re-entrance");
      requireStringEquals("0", to_string(otherSequence), "the prober's thread counted nothing");

      // busy because this very thread holds it
      UnitClaim owner(&unit, "owner");
      UnitClaim probe(&unit, "probe", UnitClaim::Reporting::Probe);
      require(!probe.ok() && probe.reentrant(), "a probe by the holder says busy (re-entrant)");
   }, captured);

   requireStringEquals("0", to_string(unit.refusedCalls()), "no probe counted on the unit");
   requireStringEquals("0", to_string(UnitState::refusedCallsOnThisThread() - sequenceBefore), "none counted for this thread");
   if (captured) {
      requireStringEquals("", output, "and none wrote a line, with logging on");
   }
   const RefusalRecord record = UnitState::threadRecord();
   require(record.operation == nullptr || strcmp(record.operation, "probe") != 0, "no probe left a record");
}

void TestUnitState::test_thread_records_belong_to_their_threads() {
   TEST_CASE("test_thread_records_belong_to_their_threads");
   UnitState unit;
   UnitState second;
   const uint64_t mainBefore = UnitState::refusedCallsOnThisThread();

   UnitClaim owner(&unit, "owner");
   require(owner.ok(), "the main thread holds the unit");

   struct Seen {
      uint64_t start = 99;
      uint64_t sequence = 0;
      uint64_t unitSerial = 0;
      string operation;
   };
   const char* const NAMES[4] = {"alpha", "beta", "gamma", "delta"};
   Seen seen[4];
   vector<thread> threads;
   for (int i = 0; i < 4; ++i) {
      threads.emplace_back([&, i] {
         seen[i].start = UnitState::refusedCallsOnThisThread();
         for (int k = 0; k < (i + 1) * 2; ++k) {
            UnitClaim refused(&unit, NAMES[i]);
         }
         const RefusalRecord record = UnitState::threadRecord();
         seen[i].sequence = record.sequence;
         seen[i].unitSerial = record.unitSerial;
         seen[i].operation = record.operation != nullptr ? record.operation : "";
      });
   }
   for (thread& t : threads) {
      t.join();
   }
   uint64_t sum = 0;
   for (int i = 0; i < 4; ++i) {
      const string who = "thread " + to_string(i) + ": ";
      requireStringEquals("0", to_string(seen[i].start), who + "a new thread starts with no refusals");
      requireStringEquals(to_string((i + 1) * 2), to_string(seen[i].sequence), who + "its own count");
      requireStringEquals(to_string(unit.serial()), to_string(seen[i].unitSerial), who + "the unit's serial, not a pointer");
      requireStringEquals(NAMES[i], seen[i].operation, who + "its own operation");
      sum += seen[i].sequence;
   }
   requireStringEquals(to_string(sum), to_string(unit.refusedCalls()), "the unit's counter is the sum of the threads'");
   requireStringEquals("0", to_string(UnitState::refusedCallsOnThisThread() - mainBefore), "the main thread's record did not move");

   // one thread refused on two units: its count covers both, its record names the latest
   UnitClaim ownerOfSecond(&second, "owner");
   uint64_t twoSequence = 0, twoSerial = 0;
   thread two([&] {
      UnitClaim a(&unit, "on the first");
      UnitClaim b(&second, "on the second");
      twoSequence = UnitState::threadRecord().sequence;
      twoSerial = UnitState::threadRecord().unitSerial;
   });
   two.join();
   requireStringEquals("2", to_string(twoSequence), "refusals on different units add up for the thread");
   requireStringEquals(to_string(second.serial()), to_string(twoSerial), "and the record names the latest unit");
}

void TestUnitState::test_thread_identifier_reuse_does_not_mix_records() {
   TEST_CASE("test_thread_identifier_reuse_does_not_mix_records");
   const int GENERATIONS = 30;
   UnitState unit;
   UnitClaim owner(&unit, "owner");
   require(owner.ok(), "the main thread holds the unit");

   // threads that start one after another, each after the last has ended: the
   // platform may give them the same identifier (and the same thread key)
   int startedAtZero = 0, endedAtThree = 0, ownUnit = 0, ownOperation = 0;
   set<thread::id> ids;
   for (int g = 0; g < GENERATIONS; ++g) {
      UnitState fresh;
      UnitClaim holdFresh(&fresh, "owner");
      uint64_t start = 99, end = 0, serial = 0;
      string operation;
      thread::id id;
      thread t([&] {
         id = this_thread::get_id();
         start = UnitState::refusedCallsOnThisThread();
         for (int k = 0; k < 3; ++k) {
            UnitClaim refused(&fresh, "generation");
         }
         const RefusalRecord record = UnitState::threadRecord();
         end = record.sequence;
         serial = record.unitSerial;
         operation = record.operation != nullptr ? record.operation : "";
      });
      t.join();
      ids.insert(id);
      startedAtZero += (start == 0) ? 1 : 0;
      endedAtThree += (end == 3) ? 1 : 0;
      ownUnit += (serial == fresh.serial()) ? 1 : 0;
      ownOperation += (operation == "generation") ? 1 : 0;
   }
   const string reuse = ids.size() < static_cast<size_t>(GENERATIONS) ? "identifiers were reused" : "no identifier was reused";
   requireStringEquals(to_string(GENERATIONS), to_string(startedAtZero), "every new thread started with no refusals (" + reuse + ": " + to_string(ids.size()) + " distinct of " + to_string(GENERATIONS) + ")");
   requireStringEquals(to_string(GENERATIONS), to_string(endedAtThree), "and counted exactly its own three");
   requireStringEquals(to_string(GENERATIONS), to_string(ownUnit), "with the serial of its own unit");
   requireStringEquals(to_string(GENERATIONS), to_string(ownOperation), "and its own operation");
}

void TestUnitState::test_unit_counter_aggregates_over_threads() {
   TEST_CASE("test_unit_counter_aggregates_over_threads");
   UnitState unit;
   const int THREADS = 3;
   const int PER_THREAD = 1000;
   atomic<bool> done(false);
   atomic<bool> decreased(false);
   uint64_t perThread[THREADS] = {0, 0, 0};

   thread reader([&] {
      uint64_t last = 0;
      while (!done.load()) {
         const uint64_t now = unit.refusedCalls();
         if (now < last) {
            decreased = true;
         }
         last = now;
      }
   });
   vector<thread> writers;
   for (int t = 0; t < THREADS; ++t) {
      writers.emplace_back([&, t] {
         for (int i = 0; i < PER_THREAD; ++i) {
            unit.recordRefusal("aggregate");
         }
         perThread[t] = UnitState::refusedCallsOnThisThread();
      });
   }
   for (thread& w : writers) {
      w.join();
   }
   done = true;
   reader.join();

   requireStringEquals(to_string(THREADS * PER_THREAD), to_string(unit.refusedCalls()), "the unit counted every refusal of every thread");
   for (int t = 0; t < THREADS; ++t) {
      requireStringEquals(to_string(PER_THREAD), to_string(perThread[t]), "thread " + to_string(t) + " counted its own");
   }
   require(!decreased.load(), "the counter never went backwards while it was being read");

   // two units do not mix
   UnitState other;
   other.recordRefusal("other");
   requireStringEquals(to_string(THREADS * PER_THREAD), to_string(unit.refusedCalls()), "another unit's refusal did not change this one");
   requireStringEquals("1", to_string(other.refusedCalls()), "and was counted on its own");

   // nothing resets it: reading, probing and claiming leave it alone
   const uint64_t before = unit.refusedCalls();
   unit.isClaimed();
   { UnitClaim claim(&unit, "granted"); }
   { UnitClaim probe(&unit, "probe", UnitClaim::Reporting::Probe); }
   requireStringEquals(to_string(before), to_string(unit.refusedCalls()), "granted claims and probes do not touch it");
}

void TestUnitState::test_logging_thresholds() {
   TEST_CASE("test_logging_thresholds");
   require(!UnitState::shouldLog(0), "0 is never logged");
   for (uint64_t n = 1; n <= 4096; ++n) {
      const bool powerOfTwo = (n == 1 || n == 2 || n == 4 || n == 8 || n == 16 || n == 32 || n == 64 || n == 128 || n == 256 ||
                               n == 512 || n == 1024 || n == 2048 || n == 4096);
      if (UnitState::shouldLog(n) != powerOfTwo) {
         require(false, "shouldLog(" + to_string(n) + ") is " + (powerOfTwo ? "true" : "false"));
         return;
      }
   }
   require(true, "1 to 4096: logged exactly at the powers of two");
   require(UnitState::shouldLog(uint64_t(1) << 63) && !UnitState::shouldLog((uint64_t(1) << 63) + 1), "also at the top of the range");

   bool captured = false;
   UnitState unit;
   unit.setLoggingEnabled(true);
   require(unit.loggingEnabled(), "logging is on");
   string output = captureStdout([&] {
      for (int i = 0; i < 20; ++i) {
         unit.recordRefusal("next()");
      }
   }, captured);
   requireStringEquals("20", to_string(unit.refusedCalls()), "20 refusals were counted");
   if (captured) {
      requireStringEquals("5", to_string(lineCount(output)), "and 5 lines were written: refusals 1, 2, 4, 8 and 16");
      for (const char* n : {"1", "2", "4", "8", "16"}) {
         require(output.find(string("refusal ") + n + " on this unit") != string::npos, string("a line for refusal ") + n);
      }
      require(output.find("refusal 3 on this unit") == string::npos && output.find("refusal 17 on this unit") == string::npos,
              "and none for the others");
      require(output.find("unit " + to_string(unit.serial()) + ",") != string::npos, "each names the unit's serial number");
      require(output.find("next()") != string::npos, "and the operation");
      require(output.find("DB Warning [ConcurrentUse]") == 0, "in the same one-line style as the error log");
   }

   // the thresholds keep going from the cumulative count
   output = captureStdout([&] {
      for (int i = 0; i < 100; ++i) {
         unit.recordRefusal("next()");
      }
   }, captured);
   requireStringEquals("120", to_string(unit.refusedCalls()), "120 in all");
   if (captured) {
      requireStringEquals("2", to_string(lineCount(output)), "refusals 21 to 120 wrote two lines: 32 and 64");
   }
}

void TestUnitState::test_logging_disabled_is_silent() {
   TEST_CASE("test_logging_disabled_is_silent");
   bool captured = false;
   UnitState unit;
   require(!unit.loggingEnabled(), "logging is off by default");
   const uint64_t threadBefore = UnitState::refusedCallsOnThisThread();
   string output = captureStdout([&] {
      for (int i = 0; i < 20; ++i) {
         unit.recordRefusal("next()");
      }
   }, captured);
   requireStringEquals("20", to_string(unit.refusedCalls()), "every refusal was counted");
   requireStringEquals("20", to_string(UnitState::refusedCallsOnThisThread() - threadBefore), "and counted for the thread");
   if (captured) {
      requireStringEquals("", output, "and nothing was written");
   }

   // switching logging on in the middle: the next thresholds are logged, none earlier
   unit.setLoggingEnabled(true);
   output = captureStdout([&] {
      for (int i = 0; i < 12; ++i) {
         unit.recordRefusal("next()");
      }
   }, captured);
   requireStringEquals("32", to_string(unit.refusedCalls()), "32 in all");
   if (captured) {
      requireStringEquals("1", to_string(lineCount(output)), "one line, for refusal 32 (the thresholds are of the cumulative count)");
   }

   // and switching it off again
   unit.setLoggingEnabled(false);
   output = captureStdout([&] {
      for (int i = 0; i < 40; ++i) {
         unit.recordRefusal("next()");
      }
   }, captured);
   requireStringEquals("72", to_string(unit.refusedCalls()), "72 in all");
   if (captured) {
      requireStringEquals("", output, "nothing written while it is off, though refusal 64 was passed");
   }
}

void TestUnitState::test_logging_follows_setLogsErrors() {
   TEST_CASE("test_logging_follows_setLogsErrors");
   unique_ptr<SQLiteDatabase> db(new SQLiteDatabase(":memory:"));
   require(db->open(), "open");
   unique_ptr<SQLiteResultSet> cursor(openCursor(*db, "select 1"));
   const shared_ptr<UnitState> unit = UnitStateAccess::of(*cursor);

   require(!unit->loggingEnabled(), "off, as the database's logging is by default");
   db->setLogsErrors(true);
   require(unit->loggingEnabled(), "setLogsErrors(true) turns it on in the shared state");
   db->setLogsErrors(false);
   require(!unit->loggingEnabled(), "setLogsErrors(false) turns it off");
   db->setLogsErrors(true);
   db.reset();
   require(unit->loggingEnabled(), "and a cursor that outlives its database still sees the last setting");
}

void TestUnitState::test_refusals_leave_the_error_snapshot_alone() {
   TEST_CASE("test_refusals_leave_the_error_snapshot_alone");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open(), "open");
   unsigned long rows = 0;
   require(db.executeUpdate("create table u(id integer primary key)", rows), "create");
   require(!db.executeUpdate("insert into u(id) values (1),(1)", rows), "a failing statement leaves a snapshot");
   const DBErrorKind kind = db.lastErrorKind();
   const int code = db.lastErrorCode();
   const string message = db.lastErrorMessage();
   require(kind == DBErrorKind::Sqlite && !message.empty(), "the snapshot is a SQLite error");

   // logging on, and a hundred simulated refusals, on the database's own unit state
   db.setLogsErrors(true);
   const shared_ptr<UnitState> unit = UnitStateAccess::of(db);
   bool captured = false;
   string output = captureStdout([&] {
      for (int i = 0; i < 100; ++i) {
         unit->recordRefusal("executeQuery");
      }
   }, captured);
   requireStringEquals("100", to_string(db.refusedCalls()), "the refusals were counted");
   require(db.lastErrorKind() == kind && db.lastErrorCode() == code && db.lastErrorMessage() == message,
           "and the error snapshot is exactly what it was");
   require(db.hadError(), "hadError() is still true");
   if (captured) {
      requireStringEquals("7", to_string(lineCount(output)), "seven lines were logged (1, 2, 4, 8, 16, 32, 64)");
      require(output.find(message) == string::npos, "none of them mentions the snapshot's message");
   }

   // a probe or a granted claim on the same state changes nothing either
   { UnitClaim probe(unit.get(), "inUse", UnitClaim::Reporting::Probe); }
   { UnitClaim claim(unit.get(), "granted"); }
   require(db.lastErrorKind() == kind && db.lastErrorMessage() == message, "still unchanged");

   // and the other direction: the snapshot being rewritten does not move the counters
   require(db.executeUpdate("insert into u(id) values (2)", rows), "a success clears the snapshot");
   require(db.lastErrorKind() == DBErrorKind::None, "cleared");
   requireStringEquals("100", to_string(db.refusedCalls()), "the counter is not affected by operations");
}

void TestUnitState::test_public_accessors_are_zero_in_ordinary_use() {
   TEST_CASE("test_public_accessors_are_zero_in_ordinary_use");
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   requireStringEquals("0", to_string(db.refusedCalls()), "a new database has refused nothing");
   require(db.open(), "open");
   unsigned long rows = 0;
   require(db.executeUpdate("create table t(id integer primary key, v text)", rows), "create");
   require(db.executeUpdate("insert into t(id, v) values (1,'a'),(2,'b'),(3,'c')", rows), "insert");
   require(!db.executeUpdate("insert into t(id, v) values (1,'x')", rows), "a failing insert");
   require(!db.executeUpdate("select 1; select 2", rows), "a refused statement (not a concurrency refusal)");
   require(db.beginTransaction() && db.executeUpdate("update t set v = 'z'", rows) && db.commit(), "a transaction");
   require(db.beginDeferredTransaction() && db.rollback(), "a rolled-back one");
   require(db.goodConnection(), "goodConnection()");
   require(db.lastInsertRowId() >= 0, "lastInsertRowId()");
   require(!db.inUse() && !db.inTransaction(), "inUse() and inTransaction()");
   {
      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select id, v from t order by id"));
      int n = 0;
      while (rs && rs->next()) {
         rs->longForColumnIndex(0);
         string s;
         rs->stringForColumn("v", s);
         ++n;
      }
      requireStringEquals("3", to_string(n), "a cursor over three rows");
      unique_ptr<SQLiteResultSet> bad(openCursor(db, "select abs(-9223372036854775807-1)"));
      require(bad && !bad->next() && bad->status() == DBResultSetStatus::Error, "a cursor that fails");
   }
   db.setShouldCacheStatements(false);
   db.setShouldCacheStatements(true);
   db.setStatementCacheLimit(1);
   db.clearCachedStatements();
   db.setBusyRetryTimeout(10);
   db.close();

   requireStringEquals("0", to_string(db.refusedCalls()), "after all of it, the database has refused nothing");
   requireStringEquals("0", to_string(Database::refusedCallsOnThisThread() - threadBefore), "and this thread has had nothing refused");

   // another thread starts at zero, and stays there while it uses a database of its own
   uint64_t otherStart = 99, otherEnd = 99, otherUnit = 99;
   thread other([&] {
      otherStart = Database::refusedCallsOnThisThread();
      SQLiteDatabase mine(":memory:");
      mine.setLogsErrors(false);
      unsigned long n = 0;
      mine.open();
      mine.executeUpdate("create table x(a)", n);
      unique_ptr<DBResultSet> rs(mine.executeQuery("select 1"));
      if (rs) {
         rs->next();
      }
      otherEnd = Database::refusedCallsOnThisThread();
      otherUnit = mine.refusedCalls();
   });
   other.join();
   requireStringEquals("0", to_string(otherStart), "a new thread: 0");
   requireStringEquals("0", to_string(otherEnd), "after using a database: still 0");
   requireStringEquals("0", to_string(otherUnit), "and its database: 0");
}

void TestUnitState::test_public_accessors_report_refusals_when_there_are_some() {
   TEST_CASE("test_public_accessors_report_refusals_when_there_are_some");
   // No database call can be refused yet, so the refusals are simulated on the unit state; the point is that
   // the two public accessors report what the state holds, for the right scope.
   SQLiteDatabase db(":memory:");
   SQLiteDatabase untouched(":memory:");
   const shared_ptr<UnitState> unit = UnitStateAccess::of(db);
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   for (int i = 0; i < 3; ++i) {
      unit->recordRefusal("simulated");
   }
   requireStringEquals("3", to_string(Database::refusedCallsOnThisThread() - threadBefore), "this thread's count rose by 3");
   requireStringEquals("3", to_string(db.refusedCalls()), "the database's count is 3");
   requireStringEquals("0", to_string(untouched.refusedCalls()), "another database's count is 0");

   // another thread: its own count is 0; the database's is the same 3 (the unit's count is not per thread)
   uint64_t otherThread = 99, otherDatabase = 99;
   thread other([&] {
      otherThread = Database::refusedCallsOnThisThread();
      otherDatabase = db.refusedCalls();
   });
   other.join();
   requireStringEquals("0", to_string(otherThread), "another thread's count is its own: 0");
   requireStringEquals("3", to_string(otherDatabase), "while the database's count is the same from any thread");

   // a refusal made by another thread counts for the database and for that thread, not for this one
   thread refused([&] {
      unit->recordRefusal("from another thread");
      otherThread = Database::refusedCallsOnThisThread();
   });
   refused.join();
   requireStringEquals("1", to_string(otherThread), "that thread's count: 1");
   requireStringEquals("4", to_string(db.refusedCalls()), "the database's count: 4");
   requireStringEquals("3", to_string(Database::refusedCallsOnThisThread() - threadBefore), "this thread's count: still 3");
}

// ---- H1a-12g: the claim and the holder key (see the memory-order notes in UnitState.h) ------------------------

void TestUnitState::test_claim_publishes_the_holder_after_it_takes_the_claim() {
   TEST_CASE("test_claim_publishes_the_holder_after_it_takes_the_claim");
   UnitState unit;
   const uintptr_t me = UnitState::currentThreadKey();

   // The window between "the claim is taken" and "the holder is stored" is the state
   // claimed-with-holder-0: tryClaim(0) leaves exactly that behind. In it nobody is
   // 'the holder': not this thread, not another, and 0 is not a key.
   require(unit.tryClaim(0), "the claim is taken with no holder published yet");
   require(unit.isClaimed(), "it is claimed");
   require(!unit.heldBy(me), "this thread is not mistaken for the holder in the window");
   require(!unit.heldBy(0), "and 0 is never a holder");
   {
      UnitClaim during(&unit, "during", UnitClaim::Reporting::Probe);
      require(!during.ok() && !during.reentrant(), "a refusal in the window is an overlap, never re-entrance");
   }
   unit.release();
   require(!unit.isClaimed() && !unit.heldBy(me), "released: free, and nobody holds it");

   // the holder is published, then withdrawn, and never outlives the claim
   {
      UnitClaim claim(&unit, "claim");
      require(claim.ok() && unit.heldBy(me), "taken: this thread holds it");
      UnitClaim nested(&unit, "nested", UnitClaim::Reporting::Probe);
      require(!nested.ok() && nested.reentrant(), "a nested claim by the holder is re-entrance");
   }
   require(!unit.heldBy(me), "after the claim ends this thread is no longer the holder");

   // release and immediate reacquisition, many times, on one thread
   bool allGranted = true;
   for (int i = 0; i < 20000; ++i) {
      UnitClaim again(&unit, "again", UnitClaim::Reporting::Probe);
      allGranted = allGranted && again.ok() && unit.heldBy(me);
   }
   require(allGranted, "release followed at once by a new claim is always granted, 20000 times");
   require(!unit.isClaimed(), "and the last release left it free");

   // another thread's stale key is not ours: A holds and releases, B holds, A is refused as overlap
   atomic<int> step(0);
   atomic<bool> classifiedAsOverlap(false);
   thread a([&] {
      { UnitClaim first(&unit, "a1", UnitClaim::Reporting::Probe); }
      step.store(1);
      while (step.load() != 2) { this_thread::yield(); }
      UnitClaim second(&unit, "a2", UnitClaim::Reporting::Probe);
      classifiedAsOverlap.store(!second.ok() && !second.reentrant());
      step.store(3);
   });
   thread b([&] {
      while (step.load() != 1) { this_thread::yield(); }
      UnitClaim mine(&unit, "b", UnitClaim::Reporting::Probe);
      step.store(2);
      while (step.load() != 3) { this_thread::yield(); }
   });
   a.join();
   b.join();
   require(classifiedAsOverlap.load(), "a thread that held and released is refused as an overlap, not re-entrance, while another holds");
}

void TestUnitState::test_claim_serializes_plain_data_under_contention() {
   TEST_CASE("test_claim_serializes_plain_data_under_contention");
   // Four threads fight for one claim. The data the claim protects is a PLAIN int and a
   // plain long: no atomic, no lock. If the claim does not give mutual exclusion, or does
   // not hand the writes of one owner to the next (the acquire/release pairing), the
   // counts are wrong and, under ThreadSanitizer, the plain accesses are reported as races.
   UnitState unit;
   int inside = 0;
   long total = 0;
   const int THREADS = 4;
   const int ROUNDS = 20000;
   atomic<long> overlapsSeen(0);
   atomic<long> wrongClassification(0);
   atomic<long> granted(0);

   vector<thread> crew;
   for (int t = 0; t < THREADS; ++t) {
      crew.emplace_back([&] {
         const uintptr_t me = UnitState::currentThreadKey();
         for (int i = 0; i < ROUNDS; ++i) {
            UnitClaim claim(&unit, "contend", UnitClaim::Reporting::Probe);
            if (claim.ok()) {
               // The plain data first, before any other atomic operation of this thread: until
               // the checks below, the claim's own compare-exchange is all that orders this
               // thread's accesses after the previous owner's (an acquire load of the claim
               // word, such as heldBy() does, or an atomic increment that the previous owner
               // also made, would order them too and hide a compare-exchange without acquire).
               const int insideNow = ++inside;
               ++total;
               // inside the claim, a nested claim is re-entrance, never an overlap
               UnitClaim nested(&unit, "nested", UnitClaim::Reporting::Probe);
               const bool nestedRight = !nested.ok() && nested.reentrant();
               --inside;
               granted.fetch_add(1);
               if (insideNow != 1) {
                  overlapsSeen.fetch_add(1);
               }
               if (!nestedRight || !unit.heldBy(me)) {
                  wrongClassification.fetch_add(1);
               }
            } else if (claim.reentrant()) {
               // refused while somebody else holds it: that is never re-entrance
               wrongClassification.fetch_add(1);
            }
         }
      });
   }
   for (thread& worker : crew) {
      worker.join();
   }

   requireStringEquals(to_string(0), to_string(overlapsSeen.load()), "two threads were never inside together");
   requireStringEquals(to_string(0), to_string(wrongClassification.load()), "re-entrance and overlap were never confused");
   requireStringEquals(to_string(granted.load()), to_string(total), "every grant was one increment of the plain counter: the owners' writes reached the next owner");
   require(granted.load() > 0, "some claims were granted (" + to_string(granted.load()) + " of " + to_string(THREADS * ROUNDS) + ")");
   require(!unit.isClaimed(), "and the claim is free at the end");
}

void TestUnitState::test_rapid_hand_off_between_two_threads() {
   TEST_CASE("test_rapid_hand_off_between_two_threads");
   // Two threads take the claim in turn, as fast as they can, passing the turn through an
   // atomic. Nothing is ever refused (the previous owner has released before it passes the
   // turn) and a plain counter written under the claim arrives intact at the other thread.
   UnitState unit;
   long shared = 0;
   atomic<int> turn(0);
   atomic<long> refused(0);
   const int ROUNDS = 20000;

   auto player = [&](int me) {
      for (int i = 0; i < ROUNDS; ++i) {
         const auto deadline = chrono::steady_clock::now() + chrono::seconds(20);
         while (turn.load(memory_order_acquire) != me) {
            if (chrono::steady_clock::now() > deadline) {
               refused.fetch_add(1000000);
               return;
            }
            this_thread::yield();
         }
         {
            UnitClaim claim(&unit, "hand-off", UnitClaim::Reporting::Probe);
            if (!claim.ok()) {
               refused.fetch_add(1);
            } else {
               ++shared;
            }
         }
         turn.store(1 - me, memory_order_release);
      }
   };
   thread first(player, 0);
   thread second(player, 1);
   first.join();
   second.join();

   requireStringEquals(to_string(0), to_string(refused.load()), "no claim made right after a hand-off was refused");
   requireStringEquals(to_string(2L * ROUNDS), to_string(shared), "every owner saw the previous owner's write");
}

// ---- H1a-12g final hardening: the claim and the holder key under hand-offs ------------------------
//
// The argument is in UnitState.h ("Why two atomics are enough"). These tests check its
// consequences. The first two use made-up keys on ONE thread, so every state of the pair
// (claimed, holder) and every order of acquisitions and releases is visited deterministically,
// whatever the hardware; the third scripts real threads step by step (every step is ordered
// by a sequentially consistent atomic, so the order is fixed, not hoped for); the fourth is
// the stress run, which is supplementary evidence and not the proof.

namespace {

const uintptr_t KEY_A = 0x1000;
const uintptr_t KEY_B = 0x2000;
const uintptr_t KEY_C = 0x3000;

// who the unit says it is held by, as seen by the three keys: "A", "B", "C", their combinations, or "-"
string believers(const UnitState& unit) {
   string who;
   if (unit.heldBy(KEY_A)) { who += "A"; }
   if (unit.heldBy(KEY_B)) { who += "B"; }
   if (unit.heldBy(KEY_C)) { who += "C"; }
   return who.empty() ? string("-") : who;
}

}

void TestUnitState::test_holder_is_classified_in_every_state_of_a_claim() {
   TEST_CASE("test_holder_is_classified_in_every_state_of_a_claim");
   // The pair (claimed, holder) has three states that a claim goes through:
   //    free (0, 0)   --tryClaim(0)-->   claimed, holder not yet published (1, 0)   --> (1, K)
   //    and back, through (1, 0) again, because release() clears the holder first.
   // (1, 0) is also the state of a release that is half way. In each state, for each key,
   // heldBy() must say 'yes' only for the key that is the holder.
   UnitState unit;
   requireStringEquals("-", believers(unit), "free: nobody");
   require(!unit.isClaimed(), "free: not claimed");

   // claimed, holder not published (the window after the compare-exchange; a release half way)
   require(unit.tryClaim(0), "the claim is taken with the holder not published yet");
   requireStringEquals("-", believers(unit), "claimed with no holder: nobody is the holder, and every key is an overlap");
   require(unit.isClaimed(), "it is claimed");
   require(!unit.tryClaim(KEY_A) && !unit.tryClaim(KEY_B) && !unit.tryClaim(KEY_C), "and nobody else gets it");
   requireStringEquals("-", believers(unit), "refusals in that state change nothing");
   unit.release();
   requireStringEquals("-", believers(unit), "released");

   // claimed by each key in turn
   const uintptr_t keys[3] = { KEY_A, KEY_B, KEY_C };
   const char* const names[3] = { "A", "B", "C" };
   for (int holder = 0; holder < 3; ++holder) {
      require(unit.tryClaim(keys[holder]), string("taken by ") + names[holder]);
      requireStringEquals(names[holder], believers(unit), string("held by ") + names[holder] + ": exactly that key is the holder");
      for (int other = 0; other < 3; ++other) {
         if (other != holder) {
            require(!unit.tryClaim(keys[other]), string(names[other]) + " is refused");
            requireStringEquals(names[holder], believers(unit),
                                string("and its refusal did not publish ") + names[other] + " as the holder, nor remove " + names[holder]);
         }
      }
      require(!unit.tryClaim(keys[holder]), string("the holder ") + names[holder] + " cannot take it twice");
      requireStringEquals(names[holder], believers(unit), "and is still the holder");
      unit.release();
      requireStringEquals("-", believers(unit), string("released by ") + names[holder] + ": nobody, in particular not " + names[holder]);
   }

   // heldBy(0) is never true, in any state
   require(!unit.heldBy(0), "0 is never a holder (free)");
   unit.tryClaim(0);
   require(!unit.heldBy(0), "0 is never a holder (claimed, holder 0)");
   unit.release();
}

void TestUnitState::test_hand_off_interleavings_are_classified_correctly() {
   TEST_CASE("test_hand_off_interleavings_are_classified_correctly");
   UnitState unit;

   // 1. A releases; B immediately acquires
   require(unit.tryClaim(KEY_A), "1: A holds");
   unit.release();
   require(unit.tryClaim(KEY_B), "1: B acquires at once");
   requireStringEquals("B", believers(unit), "1: B is the holder and A, which held it a moment ago, is not");
   unit.release();

   // 2. A releases; A immediately reacquires
   require(unit.tryClaim(KEY_A), "2: A holds");
   unit.release();
   require(unit.tryClaim(KEY_A), "2: A reacquires at once");
   requireStringEquals("A", believers(unit), "2: A is the holder");
   require(!unit.tryClaim(KEY_B), "2: B is refused");
   requireStringEquals("A", believers(unit), "2: and still A holds it");
   unit.release();

   // 3. A releases; B acquires; A attempts a claim: A is refused, and is NOT told it is the holder
   require(unit.tryClaim(KEY_A), "3: A holds");
   unit.release();
   require(unit.tryClaim(KEY_B), "3: B acquires");
   require(!unit.tryClaim(KEY_A), "3: A is refused");
   require(!unit.heldBy(KEY_A), "3: A, which held it before, does not see its own old key as the holder");
   require(unit.heldBy(KEY_B), "3: B is the holder");
   unit.release();

   // 4. A releases; B acquires; C attempts a claim
   require(unit.tryClaim(KEY_A), "4: A holds");
   unit.release();
   require(unit.tryClaim(KEY_B), "4: B acquires");
   require(!unit.tryClaim(KEY_C), "4: C is refused");
   require(!unit.heldBy(KEY_C) && !unit.heldBy(KEY_A), "4: neither C nor A is the holder");
   require(unit.heldBy(KEY_B), "4: B is");
   unit.release();

   // 5 and 6. A competing thread observes the claimed word before the holder is published, and
   // the holder while a release is under way: both are the state (claimed, holder 0) or
   // (claimed, holder K), and the previous test visited each of them for every key.
   require(unit.tryClaim(KEY_A), "5: A holds");
   unit.release();
   require(unit.tryClaim(0), "5: B has the claim word but has not published its key");
   require(!unit.tryClaim(KEY_A) && !unit.heldBy(KEY_A), "5: A (which held it earlier) is refused and is not 'the holder'");
   unit.release();

   // 7. Rapid repeated hand-offs among three: at every step exactly the new holder believes it
   // is the holder; the one that just released, and the third, do not; and the one that
   // just released is refused, as an overlap
   const uintptr_t keys[3] = { KEY_A, KEY_B, KEY_C };
   long wrong = 0;
   long handOffs = 0;
   int previous = -1;
   for (int round = 0; round < 30000; ++round) {
      const int holder = round % 3;
      if (!unit.tryClaim(keys[holder])) { ++wrong; continue; }
      ++handOffs;
      for (int other = 0; other < 3; ++other) {
         const bool believes = unit.heldBy(keys[other]);
         if (believes != (other == holder)) { ++wrong; }
         if (other != holder && unit.tryClaim(keys[other])) { ++wrong; unit.release(); }
      }
      if (previous >= 0 && unit.heldBy(keys[previous]) && previous != holder) { ++wrong; }
      previous = holder;
      unit.release();
      // the instant after the release: nobody
      if (unit.heldBy(KEY_A) || unit.heldBy(KEY_B) || unit.heldBy(KEY_C)) { ++wrong; }
   }
   requireStringEquals("30000", to_string(handOffs), "7: every hand-off was granted");
   requireStringEquals("0", to_string(wrong), "7: and no key was ever taken for the holder when it was not");
}

void TestUnitState::test_thread_keys_are_distinct_and_stable() {
   TEST_CASE("test_thread_keys_are_distinct_and_stable");
   // the claim's operations are not secretly guarded by a lock (see UnitState.h, 5b)
   require(atomic<int>::is_always_lock_free && atomic<uintptr_t>::is_always_lock_free,
           "the atomics of the claim are lock-free on this platform");
   const uintptr_t mine = UnitState::currentThreadKey();
   require(mine != 0, "a key is never 0");
   require(mine == UnitState::currentThreadKey(), "it is stable for a thread");

   // three live threads, all alive at the same time (a barrier keeps them so): distinct keys
   uintptr_t keys[3] = { 0, 0, 0 };
   atomic<int> alive(0);
   atomic<bool> release(false);
   vector<thread> crew;
   for (int i = 0; i < 3; ++i) {
      crew.emplace_back([&, i] {
         keys[i] = UnitState::currentThreadKey();
         alive.fetch_add(1);
         while (!release.load()) { this_thread::yield(); }
         if (keys[i] != UnitState::currentThreadKey()) { keys[i] = 0; }
      });
   }
   while (alive.load() < 3) { this_thread::yield(); }
   const set<uintptr_t> distinct = { mine, keys[0], keys[1], keys[2] };
   release.store(true);
   for (thread& worker : crew) { worker.join(); }
   require(keys[0] != 0 && keys[1] != 0 && keys[2] != 0, "each thread's key was stable over its life");
   requireStringEquals("4", to_string(distinct.size()), "the keys of four live threads (this one and three) are four different values");
}

void TestUnitState::test_three_threads_hand_off_in_a_fixed_order() {
   TEST_CASE("test_three_threads_hand_off_in_a_fixed_order");
   // Three real threads, A, B and C, each running its part of one script. The script
   // is ordered by 'step', a sequentially consistent atomic: a thread acts when step has
   // reached its number and then sets the next one. So the interleaving is the script's, on
   // any hardware, and the claims' own memory orders are the only thing between threads
   // beyond what step gives (step orders the threads' actions; it does not hand the
   // claim state from one thread to another, the claim's release/acquire does that).
   UnitState unit;
   atomic<int> step(0);
   atomic<int> wrongOutcomes(0);

   auto waitFor = [&](int wanted) {
      const auto deadline = chrono::steady_clock::now() + chrono::seconds(20);
      while (step.load() != wanted) {
         if (chrono::steady_clock::now() > deadline) {
            wrongOutcomes.fetch_add(1000);
            return false;
         }
         this_thread::yield();
      }
      return true;
   };
   // a probe made now must be refused and must NOT be taken for re-entrance
   auto refusedAsOverlap = [&](const char* who) {
      UnitClaim probe(&unit, who, UnitClaim::Reporting::Probe);
      if (probe.ok() || probe.reentrant()) { wrongOutcomes.fetch_add(1); }
   };

   thread a([&] {
      if (!waitFor(0)) return;
      { UnitClaim first(&unit, "a1", UnitClaim::Reporting::Probe); if (!first.ok()) { wrongOutcomes.fetch_add(1); } }
      step.store(1);
      if (!waitFor(2)) return;
      refusedAsOverlap("a: B holds, A held it earlier");
      step.store(3);
      if (!waitFor(5)) return;
      {
         UnitClaim again(&unit, "a2", UnitClaim::Reporting::Probe);      // right after B released
         if (!again.ok() || !unit.heldBy(UnitState::currentThreadKey())) { wrongOutcomes.fetch_add(1); }
         step.store(6);
         if (!waitFor(8)) return;
         UnitClaim nested(&unit, "a nested", UnitClaim::Reporting::Probe);
         if (nested.ok() || !nested.reentrant()) { wrongOutcomes.fetch_add(1); }
      }
      step.store(9);
      if (!waitFor(10)) return;
      refusedAsOverlap("a: C holds, A held it earlier");
      step.store(11);
   });
   thread b([&] {
      if (!waitFor(1)) return;
      {
         UnitClaim held(&unit, "b1", UnitClaim::Reporting::Probe);       // A released at step 0
         if (!held.ok() || !unit.heldBy(UnitState::currentThreadKey())) { wrongOutcomes.fetch_add(1); }
         step.store(2);
         if (!waitFor(4)) return;
         UnitClaim nested(&unit, "b nested", UnitClaim::Reporting::Probe);
         if (nested.ok() || !nested.reentrant()) { wrongOutcomes.fetch_add(1); }
      }
      step.store(5);
      if (!waitFor(6)) return;
      refusedAsOverlap("b: A holds, B held it earlier");
      step.store(7);
   });
   thread c([&] {
      if (!waitFor(3)) return;
      refusedAsOverlap("c: B holds, C never held it");
      step.store(4);
      if (!waitFor(7)) return;
      refusedAsOverlap("c: A holds");
      step.store(8);
      if (!waitFor(9)) return;
      {
         UnitClaim held(&unit, "c1", UnitClaim::Reporting::Probe);       // A released at step 8's end
         if (!held.ok() || !unit.heldBy(UnitState::currentThreadKey())) { wrongOutcomes.fetch_add(1); }
         step.store(10);
         if (!waitFor(11)) return;
      }
      step.store(12);
   });
   a.join();
   b.join();
   c.join();

   requireStringEquals("12", to_string(step.load()), "the script ran to its end");
   requireStringEquals("0", to_string(wrongOutcomes.load()), "every claim and refusal of the three threads came out as scripted");
   require(!unit.isClaimed(), "and the claim is free at the end");
}

void TestUnitState::test_three_party_rotation_never_shows_a_stale_key() {
   TEST_CASE("test_three_party_rotation_never_shows_a_stale_key");
   // STRESS, supplementary evidence: three threads take the claim in strict rotation (the turn
   // is passed through an atomic once the claim is released, as the contract demands).
   // While it is not its turn a thread watches heldBy() with its OWN key as hard as it can:
   // it released the claim, or has never held it, so it must never be the holder.
   UnitState unit;
   const int ROUNDS = 30000;
   atomic<int> turn(0);
   atomic<int> started(0);
   atomic<long> staleKeySeen(0);
   atomic<long> notGranted(0);
   atomic<long> wrongHolder(0);
   uintptr_t keys[3] = { 0, 0, 0 };
   long counter = 0;      // plain data, protected only by the claim

   auto player = [&](int me) {
      keys[me] = UnitState::currentThreadKey();
      started.fetch_add(1);
      while (started.load() < 3) { this_thread::yield(); }
      for (int round = 0; round < ROUNDS; ++round) {
         const auto deadline = chrono::steady_clock::now() + chrono::seconds(30);
         while (turn.load(memory_order_acquire) != me) {
            // not my turn: my own key is nobody's business
            if (unit.heldBy(keys[me])) { staleKeySeen.fetch_add(1); }
            if (chrono::steady_clock::now() > deadline) { notGranted.fetch_add(1000000); return; }
         }
         {
            UnitClaim claim(&unit, "rotate", UnitClaim::Reporting::Probe);
            if (!claim.ok()) {
               notGranted.fetch_add(1);
            } else {
               ++counter;
               if (!unit.heldBy(keys[me])) { wrongHolder.fetch_add(1); }
               if (unit.heldBy(keys[(me + 1) % 3]) || unit.heldBy(keys[(me + 2) % 3])) { wrongHolder.fetch_add(1); }
            }
         }
         if (unit.heldBy(keys[me])) { staleKeySeen.fetch_add(1); }    // the moment after my release
         turn.store((me + 1) % 3, memory_order_release);
      }
   };
   thread t0(player, 0);
   thread t1(player, 1);
   thread t2(player, 2);
   t0.join();
   t1.join();
   t2.join();

   requireStringEquals("0", to_string(staleKeySeen.load()), "no thread ever saw its own old key as the holder");
   requireStringEquals("0", to_string(notGranted.load()), "no hand-off was refused");
   requireStringEquals("0", to_string(wrongHolder.load()), "the holder was always the thread that had the claim");
   requireStringEquals(to_string(3L * ROUNDS), to_string(counter), "and each owner saw its predecessor's write");
}
