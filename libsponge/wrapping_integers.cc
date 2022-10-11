#include <cassert>
#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template<typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

const uint64_t _mod = 1ul << 32;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
   n = (n % _mod + isn.raw_value()) % _mod;
   return WrappingInt32{static_cast<uint32_t>(n)};
   /* return {isn + n};
    * 好像是因为可以自动取模, 所以不会出错?
    */
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.

uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
   uint64_t ab = (n.raw_value() - isn.raw_value() + _mod) % _mod; // 最小合法的
   int t = checkpoint > ab ? (checkpoint - ab) / _mod : 0;
   assert(t >= 0); // cause ab < _mod, even if checkpoint < ab, t will be 0 !
   uint64_t x = ab + t * _mod, y = ab + (t + 1) * _mod;
   uint64_t d1 = x > checkpoint ? x - checkpoint : checkpoint - x;
   uint64_t d2 = y > checkpoint ? y - checkpoint : checkpoint - y;
   return d1 < d2 ? x : y;
}