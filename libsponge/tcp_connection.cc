/**
 * reference: https://github.com/PKUFlyingPig/CS144-Computer-Network
 */
#include "tcp_connection.hh"

#include <iostream>

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received_counter; }

bool TCPConnection::handle_sender_segments() {
  bool isSend = false;
  queue<TCPSegment> &sender_segments = _sender.segments_out();
  while (!sender_segments.empty()) {
    isSend = true;
    TCPSegment segment = sender_segments.front();
    sender_segments.pop();
    set_ack_win(segment);
    _segments_out.push(segment);
  }
  return isSend;
}

void TCPConnection::set_rst_state(bool send_rst) {
  _receiver.stream_out().set_error();
  _sender.stream_in().set_error();
  _linger_after_streams_finish = false;
  _active = false;
  if (send_rst) {
    TCPSegment rst_seg;
    rst_seg.header().rst = true;
    _segments_out.push(rst_seg);
  }
}

void TCPConnection::segment_received(const TCPSegment &seg) {
  _time_since_last_segment_received_counter = 0;
  const TCPHeader &header = seg.header();
  // if the rst (reset) flag is set, sets both the inbound and outbound streams
  // to the error state and kills the connection permanently
  if (header.rst) {
    set_rst_state(false);
    return;
  }
  // gives the segment to the TCPReceiver so it can inspect the fields it cares about on
  // incoming segments: seqno, syn , payload, and fin
  _receiver.segment_received(seg);

  // 如果是 listen 到了 SYN,然后发出的时候因为有了ackno,所以会带上ACK
  if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
      TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
    // 此时肯定是第一次调用 fill_window，因此会发送 SYN + ACK
    connect();
    return;
  }
  // if the TCPConnection's inbound stream ends before
  // the TCPConnection has ever sent a fin segment,
  // then the TCPConnection doesn't need to linger after both streams finish
  if (check_inbound_ended() && !_sender.fin_sent()) {
    _linger_after_streams_finish = false;
  }

  // tells the TCPSender about the fields it cares about
  // on incoming segments: ackno and window size
  if (header.ack) {
    _sender.ack_received(header.ackno, header.win);
  }
  // 收到报文段后可能是 inbound end 和 outbound_ended_acked 条件满足
  if (check_inbound_ended() && check_outbound_ended_acked()) {
    if (!_linger_after_streams_finish) {
      _active = false;
      return;
    }
  }
  // if the incoming segment occupied any sequence numbers, the TCPConnection makes
  // sure that at least one segment is sent in reply,
  // to reflect an update in the ackno and window size.
  if (seg.length_in_sequence_space() > 0) {
    _sender.send_empty_segment();
  }
  handle_sender_segments();
}
/**
 * 设置即将发送的报文段头部字段: ACK, ackno, win
 * @param segment
 */
void TCPConnection::set_ack_win(TCPSegment &segment) {
  optional<WrappingInt32> ackno = _receiver.ackno();
  if (ackno.has_value()) {
    segment.header().ack = true;
    segment.header().ackno = ackno.value();
  }
  segment.header().win = static_cast<uint16_t>(_receiver.window_size());
}
/**
 * Initiate a connection by sending a SYN segment
 */
void TCPConnection::connect() {
  // send SYN
  _sender.fill_window();
  handle_sender_segments();
}
/**
 * Write data to the outbound byte stream, and send it over TCP if possible
 * the number of bytes from `data` that were actually written.
 * @param data
 * @return number of actually written bytes
 */
size_t TCPConnection::write(const string &data) {
  size_t writed = _sender.stream_in().write(data);
  _sender.fill_window();
  handle_sender_segments();
  return writed;
}
/**
 * Shut down the outbound byte stream (still allows reading incoming data)
 * sender数据已经发送完了
 */
void TCPConnection::end_input_stream() {
  _sender.stream_in().end_input();
  // may send FIN
  _sender.fill_window();
  handle_sender_segments();
}

// prereq 1 : The inbound stream has been fully assembled and has ended.
bool TCPConnection::check_inbound_ended() {
  return _receiver.unassembled_bytes() == 0 && _receiver.stream_out().input_ended();
}
// prereq 2 : The outbound stream has been ended by the local application and fully sent
// (including the fact that it ended, i.e. a segment with fin ) to the remote peer.
// prereq 3 : The outbound stream has been fully acknowledged by the remote peer.
bool TCPConnection::check_outbound_ended_acked() {
  return _sender.stream_in().eof() && _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 &&
         _sender.bytes_in_flight() == 0;
}
// 1) tell the TCPSender about the passage of time
// 2) abort the connection, and send a reset segment to the peer (an empty segment with
//    the rst flag set), if the number of consecutive retransmissions is more than an upper
//    limit TCPConfig::MAX RETX ATTEMPTS.
// 3) end the connection cleanly if necessary
//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
  _time_since_last_segment_received_counter += ms_since_last_tick;
  // tick the sender to do the retransmit
  _sender.tick(ms_since_last_tick);
  // abort the connection
  if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
    // an empty segment with rst set
    set_rst_state(true);
    return;
  }
  // if new retransmit segment generated, send it
  if (_sender.segments_out().size() > 0) {
    TCPSegment retx_seg = _sender.segments_out().front();
    _sender.segments_out().pop();
    set_ack_win(retx_seg);
    _segments_out.push(retx_seg);
  }
  // At any point where prerequisites #1 through #3 are satisfied, the connection is “done”
  // (and active() should return false) if linger after streams finish is false.
  if (check_inbound_ended() && check_outbound_ended_acked()) {
    if (!_linger_after_streams_finish || _time_since_last_segment_received_counter >= 10 * _cfg.rt_timeout) {
      _active = false;
    }
  }
}

TCPConnection::~TCPConnection() {
  try {
    if (active()) {
      cerr << "Warning: Unclean shutdown of TCPConnection\n";
      // Your code here: need to send a RST segment to the peer
      set_rst_state(true);
    }
  } catch (const exception &e) {
    std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
  }
}