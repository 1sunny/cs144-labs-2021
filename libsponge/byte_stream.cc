#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template<typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : cap(capacity) {
}

size_t ByteStream::write(const string &data) {
    size_t rem = remaining_capacity();
    size_t can_wr = data.size();
    if (data.size() > rem) {
        can_wr = rem;
    }
    this->wr += can_wr;
    for (size_t i = 0; i < can_wr; ++i) {
        q.push_back(data[i]);
    }
    return can_wr;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    return string().assign(q.begin(), q.begin() + min(len, q.size()));
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t can_pop = min(q.size(), len);
    this->rd += can_pop;
    for (size_t i = 0; i < can_pop; ++i) {
        q.pop_front();
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    const string &output = peek_output(len);
    pop_output(len);
    return output;
}

void ByteStream::end_input() {
    input_end = true;
}

bool ByteStream::input_ended() const {
    return input_end;
}

size_t ByteStream::buffer_size() const {
    return q.size();
}

bool ByteStream::buffer_empty() const {
    return !q.size();
}

bool ByteStream::eof() const {
    return input_ended() && !q.size();
}

size_t ByteStream::bytes_written() const {
    return this->wr;
}

size_t ByteStream::bytes_read() const {
    return this->rd;
}

size_t ByteStream::remaining_capacity() const {
    return this->cap - q.size();
}