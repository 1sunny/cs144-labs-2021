#include "tcp_sender.hh"
#include "tcp_config.hh"
#include <random>

template<typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
        : _isn(fixed_isn.value_or(WrappingInt32{random_device()()})), _initial_retransmission_timeout{retx_timeout},
          _stream(capacity), _ackno(0), _remote_win(1), _bytes_in_flight(0), timer(retx_timeout) {}

void TCPSender::send_segments(TCPSegment &seg) {
  seg.header().seqno = next_seqno();
  _next_seqno += seg.length_in_sequence_space();
  _bytes_in_flight += seg.length_in_sequence_space();
  _segments_out.push(seg);
  _segments_outstanding.push(seg);
  // Every time a segment containing data (nonzero length in sequence space) is sent
  // (whether it’s the first time or a retransmission),
  // if the timer is not running, start it running
  // ERROR: Timer doesn't restart without ACK of new data
  if (!timer.running()) {
    timer.start();
  }
}

void TCPSender::fill_window() {
  // CLOSED -> stream waiting to begin
  if (!_syn_sent) {
    _syn_sent = true;
    TCPSegment seg;
    seg.header().syn = true;
    seg.header().seqno = next_seqno();
    send_segments(seg);
    return;
  }
  // SYN_SENT -> stream start but nothing acknowledged
  if (_next_seqno == _bytes_in_flight) {
    return;
  }

  size_t window_size = _remote_win == 0 ? 1 : _remote_win;
  size_t remain = 0;

  while ((remain = window_size - (_next_seqno - _ackno))) {
    TCPSegment seg;
    size_t len = TCPConfig::MAX_PAYLOAD_SIZE > remain ? remain : TCPConfig::MAX_PAYLOAD_SIZE;
    // SYN_ACKED -> stream ongoing
    if (!_stream.eof()) {
      seg.payload() = Buffer(_stream.read(len));
      if (_stream.eof() && remain - seg.length_in_sequence_space() > 0){
        seg.header().fin = true;
        _fin_sent = true;
      }
      if (seg.length_in_sequence_space() == 0)
        return;
      send_segments(seg);
    }
    // SYN_ACKED -> stream ongoing (stream has reached EOF but FIN hasn't been send yet)
    else if (_stream.eof()) {
      // remain > 0 && FIN haven't been sent
      if (_next_seqno <= _stream.bytes_written() + 1) {
        seg.header().fin = true;
        _fin_sent = true;
        send_segments(seg);
      }
      // FIN_SENT and FIN_ACKED both do nothing Just return
      else
        return;
    }
  }
}

void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
  uint64_t abs_ackno = unwrap(ackno, _isn, _ackno);
  // 确认还未发送的
  if (abs_ackno > _next_seqno) {
    return;
  }
  _remote_win = static_cast<size_t>(window_size);
  // 已经被确认过
  if (abs_ackno <= _ackno) {
    return;
  }
  _ackno = abs_ackno;

  timer.init_rto();
  timer.start();
  _consecutive_retransmissions = 0;

  while (!_segments_outstanding.empty()) {
    TCPSegment seg = _segments_outstanding.front();
    if (ackno.raw_value() >= seg.header().seqno.raw_value()
                            + static_cast<uint32_t>(seg.length_in_sequence_space())){
      _bytes_in_flight -= seg.length_in_sequence_space();
      _segments_outstanding.pop();
    }else{
      break;
    }
  }
  if (_segments_outstanding.empty()) {
    timer.shutdown();
  }
  fill_window();
}

void TCPSender::tick(const size_t ms_since_last_tick) {
  if (timer.expired(ms_since_last_tick)) {
    if (_remote_win > 0) {
      _consecutive_retransmissions++;
      timer.double_rto();
    }
    timer.start();
    _segments_out.push(_segments_outstanding.front());
  }
}

void TCPSender::send_empty_segment() {
  TCPSegment seg;
  seg.header().seqno = next_seqno();
  _segments_out.push(seg);
}
// #include "tcp_sender.hh"
// #include "tcp_config.hh"
//
// #include <random>
// #include <iostream>
// #include <cassert>
//
// using namespace std;
//
// enum Flag {
//     URG = 0x100000, ACK = 0x010000, PSH = 0x001000, RST = 0x000100, SYN = 0x000010, FIN = 0x000001
// };
//
// //! \param[in] capacity the capacity of the outgoing byte stream
// //! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
// //! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
// TCPSender::TCPSender(const size_t capacity,
//                      const uint16_t retx_timeout,
//                      const std::optional<WrappingInt32> fixed_isn)
//         : _isn(fixed_isn.value_or(WrappingInt32{random_device()()})),
//           _initial_retransmission_timeout{retx_timeout},
//           _stream(capacity),
//           _timer(retx_timeout) {}
//
// void TCPSender::send(TCPSegment seg) {
//   seg.header().seqno = wrap(_next_seqno, _isn);
//   _outstanding[_next_seqno] = seg;
//   size_t length = seg.length_in_sequence_space();
//   _next_seqno += length;
//   _bytes_in_flight += length;
//   segments_out().push(seg);
//   // Every time a segment containing data (nonzero length in sequence space) is sent
//   // (whether it’s the first time or a retransmission),
//   // if the timer is not running, start it running
//   // ERROR: TCPTimer doesn't restart without ACK of new data
//   if (!_timer.running()) {
//     _timer.start();
//   }
// }
//
// void TCPSender::fill_window() {
//   if (!_syn_sent) {
//     // send(get_segment(_next_seqno, SYN));
//     TCPSegment seg;
//     seg.header().syn = true;
//     send(seg);
//     _syn_sent = true;
//     return;
//   }
//   // If SYN has not been acked, do nothing.
//   if (!_outstanding.empty() && _outstanding.begin()->second.header().syn)
//     return;
//   if (_fin_sent){
//     return;
//   }
//   while (!_stream.buffer_empty()) {
//     size_t bytes_to_send = min({_stream.buffer_size(), allow_to_send(), TCPConfig::MAX_PAYLOAD_SIZE});
//     // windows size not enough
//     if (bytes_to_send == 0) {
//       break;
//     }
//     string data = _stream.read(bytes_to_send);
//     // Piggyback FIN in segment when space is available
//     bool send_fin = false;
//     // No need to check _fin_sent
//     if (_stream.eof() && allow_to_send() - data.size() >= 1) {
//       send_fin = true;
//       // error: Retransmit a FIN-containing segment same as any other
//       _fin_sent = true;
//     }
//     TCPSegment seg;
//     seg.header().fin = send_fin;
//     seg.payload() = Buffer(move(data));
//     send(seg);
//   }
//   // stream eof, send FIN
//   if (!_fin_sent && _stream.eof() && allow_to_send() > 0) {
//     TCPSegment seg;
//     seg.header().fin = true;
//     send(seg);
//     // error: Expectation: in state `stream finished and fully acknowledged
//     _fin_sent = true;
//   }
// }
//
// // See test code send_window.cc line 113 why the commented code is wrong.
// bool TCPSender::ack_valid(uint64_t abs_ackno) {
//   if (_outstanding.empty())
//     return abs_ackno <= _next_seqno;
//   return abs_ackno <= _next_seqno &&
//          //  abs_ackno >= unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno) +
//          //          _segments_outstanding.front().length_in_sequence_space();
//          abs_ackno >= unwrap(_outstanding.begin()->second.header().seqno, _isn, _next_seqno);
// }
//
// //! \param ackno The remote receiver's ackno (acknowledgment number)
// //  the ackno reflects an absolute sequence number bigger than any previous ackno
// //! \param window_size The remote receiver's advertised window size
// /*
// (a) Set the RTO back to its “initial value.”
// (b) If the sender has any outstanding data, restart the retransmission _timer so that it
//     will expired after RTO milliseconds (for the current value of RTO).
// (c) Reset the count of “consecutive retransmissions” back to zero
//  */
// void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
//   // uint64_t remote_expected_abs = unwrap(ackno, _isn, _stream.bytes_read() + 1);
//   uint64_t remote_expected_abs = unwrap(ackno, _isn, _next_seqno);
//   // Impossible ackno (beyond next seqno) is ignored(确认了还没发送的seq)
//   // if (remote_expected_abs > _next_seqno) {
//   //   return;
//   // }
//   if (!ack_valid(remote_expected_abs)) {
//     return;
//   }
//   // _timer.init_rto(); // 找到了才重置
//
//   // remove acked in-flight segments
//   for (auto it = _outstanding.begin(); it != _outstanding.end();) {
//     auto nxt = ++it;
//     --it;
//     const TCPSegment &seg = it->second;
//     if (it->first + seg.length_in_sequence_space() <= remote_expected_abs) {
//       _bytes_in_flight -= seg.length_in_sequence_space();
//       _outstanding.erase(it);
//
//       _timer.init_rto();
//       // TCPTimer doesn't restart without ACK of new data(两次 ackno 相同,第二次不应该重置时间)
//       _timer.start();
//       _consecutive_retransmissions = 0;
//     } else {
//       break;
//     }
//     it = nxt;
//   }
//   // When all outstanding data has been acknowledged, stop the retransmission timer.
//   if (_outstanding.empty()) {
//     _timer.reset();
//   }
//   // _abs_seq_limit = maximum sendable abs_seq + 1 (no matter the remote window size is 0)
//   _abs_seq_limit = remote_expected_abs + max(static_cast<uint16_t>(1), window_size);
//   _remote_win = window_size;
//   fill_window();
// }
//
// /*
// If tick is called and the retransmission timer has expired:
// 1) Retransmit the earliest (lowest sequence number) segment
// 2) If the window size is nonzero:
//     1.Keep track of the number of consecutive retransmissions, and increment it
//     2.Double the value of RTO
// 3) Reset the retransmission _timer and start it such that it expires after RTO
//  */
// //! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
// void TCPSender::tick(const size_t ms_since_last_tick) {
//   if (_timer.expired(ms_since_last_tick)) {
//     // retransmit
//     assert(_outstanding.size() > 0);
//     _segments_out.push(_outstanding.begin()->second);
//     // ???
//     if (_remote_win > 0 || _outstanding.begin()->second.header().syn) {
//       _consecutive_retransmissions++;
//       _timer.double_rto();
//     }
//     _timer.start();
//   }
// }
//
// // send a TCPSegment that has zero length in sequence space,
// // and with the sequence number set correctly
// void TCPSender::send_empty_segment() {
//   // 下面这个没有增加 _next_seqno
//   TCPSegment seg;
//   seg.header().seqno = next_seqno();
//   _segments_out.push(seg);
// }
// --------------------------------------------------------------
// #include "tcp_sender.hh"
//
// #include "tcp_config.hh"
//
// #include <random>
// // #include <iostream>
// #include <algorithm>
//
// // Dummy implementation of a TCP sender
//
// // For Lab 3, please replace with a real implementation that passes the
// // automated checks run by `make check_lab3`.
//
// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}
//
// using namespace std;
//
// //! \param[in] capacity the capacity of the outgoing byte stream
// //! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
// //! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
// TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
//         : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
//         , _initial_retransmission_timeout{retx_timeout}
//         , _stream(capacity)
//         , _timer{retx_timeout}
//         , _rto{retx_timeout}{}
//
// void TCPSender::fill_window() {
//   if (!_syn_sent) {
//     _syn_sent = true;
//     TCPSegment seg;
//     seg.header().syn = true;
//     _send_segment(seg);
//     return;
//   }
//   // If SYN has not been acked, do nothing.
//   if (!_segments_outstanding.empty() && _segments_outstanding.front().header().syn)
//     return;
//   // If _stream is empty but input has not ended, do nothing.
//   if (_stream.buffer_empty() && !_stream.eof())
//     // Lab4 behavior: if incoming_seg.length_in_sequence_space() is not zero, send ack.
//     return;
//   if (_fin_sent)
//     return;
//
//   if (_receiver_window_size) {
//     while (_receiver_free_space) {
//       TCPSegment seg;
//       size_t payload_size = min({_stream.buffer_size(),
//                                  static_cast<size_t>(_receiver_free_space),
//                                  static_cast<size_t>(TCPConfig::MAX_PAYLOAD_SIZE)});
//       seg.payload() = Buffer{_stream.read(payload_size)};
//       if (_stream.eof() && static_cast<size_t>(_receiver_free_space) > payload_size) {
//         seg.header().fin = true;
//         _fin_sent = true;
//       }
//       // if (seg.length_in_sequence_space())
//       _send_segment(seg);
//       if (_stream.buffer_empty())
//         break;
//     }
//   }
//   else if (_receiver_free_space == 0) {
//     // The zero-window-detect-segment should only be sent once (retransmition excute by tick function).
//     // Before it is sent, _receiver_free_space is zero.*** Then it will be -1. ***
//     TCPSegment seg;
//     if (_stream.eof()) {
//       seg.header().fin = true;
//       _fin_sent = true;
//       _send_segment(seg);
//     } else if (!_stream.buffer_empty()) {
//       seg.payload() = Buffer{_stream.read(1)};
//       _send_segment(seg);
//     }
//   }
// }
//
// //! \param ackno The remote receiver's ackno (acknowledgment number)
// //! \param window_size The remote receiver's advertised window size
// void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
//   uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
//   if (!_ack_valid(abs_ackno)) {
//     return;
//   }
//   _receiver_window_size = window_size;
//   _receiver_free_space = window_size;
//   while (!_segments_outstanding.empty()) {
//     TCPSegment seg = _segments_outstanding.front();
//     if (unwrap(seg.header().seqno, _isn, _next_seqno) + seg.length_in_sequence_space() <= abs_ackno) {
//       _bytes_in_flight -= seg.length_in_sequence_space();
//       _segments_outstanding.pop();
//       // Do not do the following operations outside while loop.
//       // Because if the ack is not corresponding to any segment in the segment_outstanding,
//       // we should not restart the timer.
//       _consecutive_retransmissions = 0;
//       _timer.init_rto();
//       _timer.start();
//     } else {
//       break;
//     }
//   }
//   if (!_segments_outstanding.empty()) {
//     _receiver_free_space = static_cast<uint16_t>(
//             abs_ackno + static_cast<uint64_t>(window_size) -
//             unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno) - _bytes_in_flight);
//   }
//
//   if (_bytes_in_flight == 0)
//     _timer.shutdown();
//
//   fill_window();
// }
//
// //! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
// void TCPSender::tick(const size_t ms_since_last_tick) {
//   if(_timer.expired(ms_since_last_tick)){
//     _segments_out.push(_segments_outstanding.front());
//     if (_receiver_window_size || _segments_outstanding.front().header().syn) {
//       ++_consecutive_retransmissions;
//       _timer.double_rto();
//     }
//     _timer.start();
//   }
// }
//
// void TCPSender::send_empty_segment() {
//   TCPSegment seg;
//   seg.header().seqno = next_seqno();
//   _segments_out.push(seg);
// }
//
// bool TCPSender::_ack_valid(uint64_t abs_ackno) {
//   if (_segments_outstanding.empty())
//     return abs_ackno <= _next_seqno;
//   return abs_ackno <= _next_seqno && abs_ackno >= unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno);
// }
//
// void TCPSender::_send_segment(TCPSegment &seg) {
//   seg.header().seqno = wrap(_next_seqno, _isn);
//   _segments_outstanding.push(seg);
//   size_t length = seg.length_in_sequence_space();
//   _next_seqno += length;
//   _bytes_in_flight += length;
//   if (_syn_sent) // ? ok
//     _receiver_free_space -= seg.length_in_sequence_space();
//   segments_out().push(seg);
//   // Every time a segment containing data (nonzero length in sequence space) is sent
//   // (whether it’s the first time or a retransmission),
//   // if the timer is not running, start it running
//   // ERROR: TCPTimer doesn't restart without ACK of new data
//   if (!_timer.running()) {
//     _timer.start();
//   }
// }
// --------------------------------------------------------------
