#include <cassert>
#include "stream_reassembler.hh"

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

bool StreamReassembler::seg_overlap(const Seg &s1, const Seg &s2) const{
    return !(s1.r + 1 < s2.l || s1.l > s2.r + 1);
}

void StreamReassembler::merge_seg(Seg &s1, const Seg &s2) const{
    if (s1.l > s2.l) {
        s1.data = s2.data.substr(0, (s1.l - 1) - s2.l + 1) + s1.data;
        s1.l = s2.l;
    }
    if (s1.r < s2.r) {
        s1.data = s1.data + s2.data.substr(s1.r + 1 - s2.l);
        s1.r = s2.r;
    }
}

void StreamReassembler::handle_overlap(Seg &seg) {
    for (auto it = _st.begin(); it != _st.end();) {
        auto nxt = ++it;
        it--;
        if (seg_overlap(*it, seg)) {
            merge_seg(seg, *it);
            _st.erase(it);
        }
        it = nxt;
    }
    _st.insert(seg);
}

void StreamReassembler::set_assembled(ByteStream& stream) {
    _unassembled_bytes = 0;
    for (auto it = _st.begin(); it != _st.end();) {
        auto nxt = ++it;
        --it;
        if ((*it).l == _first_unassembled) {
            stream.write((*it).data);
            _first_unassembled = (*it).r + 1;
            _st.erase(it);
        } else {
            _unassembled_bytes += (*it).data.size();
        }
        it = nxt;
    }
}

void StreamReassembler::set_stream_end(ByteStream& stream) {
    if (_eof && _first_unassembled == _end_idx) {
        stream.end_input();
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 中间出现空串的话会出错
    ByteStream &stream = stream_out();
    size_t end_iter = index + data.size();
    if (eof) {
        _end_idx = end_iter;
        _eof = true;
    }
    set_stream_end(stream);

    size_t l, r;
    size_t first_unaccept = first_unacceptable();
    if (index >= first_unaccept) return;
    if (end_iter <= _first_unassembled) return;
    r = min(end_iter - 1, first_unaccept - 1);
    l = max(index, _first_unassembled);
    assert(l <= r);
    string _data = data.substr(l - index, r - l + 1); // (r-index)-(l-index)+1

    Seg seg = {l, r,  index, _data};
    handle_overlap(seg);
    set_assembled(stream);
    set_stream_end(stream);
}

size_t StreamReassembler::unassembled_bytes() const {
    return _unassembled_bytes;
}

bool StreamReassembler::empty() const {
    return unassembled_bytes() == 0;
}