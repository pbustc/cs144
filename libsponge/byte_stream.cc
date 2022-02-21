#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _capacity(capacity), _msg_queue(), _bytes_written(0), _bytes_read(0), _input_end(false) {}

size_t ByteStream::write(const string &data) {
    if (_input_end)
        return 0;
    size_t write_size = min(data.size(), remaining_capacity());

    // 保证最大容量为capacity
    for (size_t i = 0; i < write_size; ++i) {
        _msg_queue.push_back(data[i]);
    }
    _bytes_written += write_size;
    return write_size;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t peek_size = min(len, buffer_size());
    return string(_msg_queue.begin(), _msg_queue.begin() + peek_size);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t pop_size = min(len, buffer_size());
    _bytes_read += pop_size;
    for (size_t i = 0; i < pop_size; ++i) {
        _msg_queue.pop_front();
    }
    return;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string readstr = peek_output(len);
    pop_output(len);
    return readstr;
}

void ByteStream::end_input() { _input_end = true; }

bool ByteStream::input_ended() const { return _input_end; }

size_t ByteStream::buffer_size() const { return _msg_queue.size(); }

bool ByteStream::buffer_empty() const { return _msg_queue.empty(); }

bool ByteStream::eof() const { return _input_end && _msg_queue.empty(); }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _msg_queue.size(); }
