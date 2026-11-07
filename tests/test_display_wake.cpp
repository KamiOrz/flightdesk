#include "display_wake.h"
#include <cassert>
#include <iostream>

int main() {
  DisplayWake wake;
  assert(!wake.shouldLight(0,false,false));
  assert(wake.shouldLight(1000,true,false));
  assert(!wake.shouldLight(2000,false,false)); // Last aircraft leaves or expires.
  wake.peek(3000);
  assert(wake.shouldLight(3000,false,false));
  assert(wake.shouldLight(17999,false,false));
  assert(!wake.shouldLight(18000,false,false));
  wake.peek(20000);
  assert(wake.shouldLight(40000,true,false)); // Aircraft outlasts manual peek.
  assert(!wake.shouldLight(40001,false,false));
  assert(wake.shouldLight(41000,false,true)); // Setup remains accessible.
  assert(!wake.shouldLight(42000,false,false));
  wake.peek(UINT32_MAX-1000);
  assert(wake.shouldLight(0,false,false));
  assert(wake.shouldLight(13998,false,false));
  assert(!wake.shouldLight(13999,false,false));
  // Expired peeks do not reappear after another rollover.
  assert(!wake.shouldLight(UINT32_MAX-1000,false,false));
  std::cout << "Idle, aircraft arrival/departure, manual wake, setup and rollover: passed\n";
}
