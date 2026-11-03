#ifndef TEST_UNIT_STATE_H
#define TEST_UNIT_STATE_H

#include "TestSuite.h"


/**
 * Tests of the unit-state infrastructure (H1a-12f): the state that a database and
 * its result sets share, its serial numbers, the claim primitive, the per-thread
 * refusal record, the aggregate counter, the logging helper and the non-reporting
 * probe.
 *
 * These tests use the primitive on its own, on a UnitState of their own or on the
 * state of a database that is idle, and never run two threads inside a database at
 * once. The simulated refusals are made directly on the state (UnitState::
 * recordRefusal / UnitClaim); refusals made by real calls are TestUnitClaims and the
 * "refusal_*" scenarios of the regression harness (H1a-12g).
 *
 * Worker threads do not call require(): they record what they saw, and the main
 * thread checks it after joining them.
 */
class TestUnitState : public poivre::TestSuite {
protected:
   void runTests();

   void test_database_and_cursors_share_one_unit_state();
   void test_cursor_keeps_the_unit_state_after_the_database_is_destroyed();
   void test_independent_databases_have_different_serials();
   void test_serial_assignment_under_concurrent_construction();
   void test_claim_has_one_owner_and_refuses_the_rest();
   void test_nested_claim_is_told_from_overlap();
   void test_released_claim_allows_a_hand_off();
   void test_probe_reports_nothing();
   void test_thread_records_belong_to_their_threads();
   void test_thread_identifier_reuse_does_not_mix_records();
   void test_unit_counter_aggregates_over_threads();
   void test_logging_thresholds();
   void test_logging_disabled_is_silent();
   void test_logging_follows_setLogsErrors();
   void test_refusals_leave_the_error_snapshot_alone();
   void test_public_accessors_are_zero_in_ordinary_use();
   void test_public_accessors_report_refusals_when_there_are_some();
   void test_claim_publishes_the_holder_after_it_takes_the_claim();
   void test_claim_serializes_plain_data_under_contention();
   void test_rapid_hand_off_between_two_threads();

   // H1a-12g final hardening: the claim and the holder key under hand-offs
   void test_holder_is_classified_in_every_state_of_a_claim();
   void test_hand_off_interleavings_are_classified_correctly();
   void test_thread_keys_are_distinct_and_stable();
   void test_three_threads_hand_off_in_a_fixed_order();
   void test_three_party_rotation_never_shows_a_stale_key();

public:
   TestUnitState();

};

#endif
