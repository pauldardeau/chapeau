#ifndef TESTS_H
#define TESTS_H


class Tests {
public:
   // runs every suite; returns the process exit code (0 = everything passed
   // and at least one assertion ran, 1 = a failure or an empty run)
   int run();
};

#endif
