#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;
/**
 *  \brief 当前 TCPReceiver 大体上有三种状态， 分别是
 *      1. LISTEN，此时 SYN 包尚未抵达。可以通过 syn 标志位来判断是否在当前状态
 *      2. SYN_RECV, 此时 SYN 抵达。只能判断当前不在 1、3状态时才能确定在当前状态
 *      3. FIN_RECV, 此时 FIN 抵达。可以通过 ByteStream end_input 来判断是否在当前状态
 */
void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &header = seg.header();
    if (!syn && header.syn) {
        syn = true;
        ISN = header.seqno;
    }
    /*
     fix bug(add): && seg.length_in_sequence_space()
     */
    if(syn && seg.length_in_sequence_space()){
        // checkpoint: A recent absolute 64-bit sequence number
        uint64_t abs_seq = unwrap(header.seqno, ISN, _reassembler.first_unassembled());
        // SYN时求出来 abs_seq = 0, 没有steam_index,所以为了兼容reassembler, 给个0去
        uint64_t stream_index = abs_seq - 1 + (header.syn);
        _reassembler.push_substring(seg.payload().copy(), stream_index, header.fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!syn) return nullopt;
    // 如果不在 LISTEN 状态，则 ackno 还需要加上一个 SYN 标志的长度
    uint64_t abs_seq = _reassembler.first_unassembled() + 1;
    // 如果当前处于 FIN_RECV 状态，则还需要加上 FIN 标志长度
    /*只有建立了连接,同时FIN来过,而且字符流重组完成才能为ack_no+1*/
    abs_seq += _reassembler.stream_out().input_ended();
    return WrappingInt32{wrap( abs_seq, ISN)};
}

size_t TCPReceiver::window_size() const {
    return _reassembler.first_unacceptable() - _reassembler.first_unassembled();
}