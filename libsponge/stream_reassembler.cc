#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _reassembler_buffer(capacity, '\0')
    , _flag(capacity, false)
    , _bytes_unreassembled(0)
    , _eof(false)
    , _eof_index(0)
    , _output(capacity)
    , _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t first_unread = _output.bytes_written();
    size_t first_unaccpetable = first_unread + _capacity;
    // there is nothing need read
    if (index > first_unaccpetable || index + data.size() < first_unread)
        return;

    size_t push_begin = max(index, first_unread);
    size_t push_end = min(index + data.size(), first_unaccpetable);

    for (size_t i = push_begin; i < push_end; ++i) {
        if (!_flag[i - first_unread]) {
            _flag[i - first_unread] = true;
            _reassembler_buffer[i - first_unread] = data[i - index];
            ++_bytes_unreassembled;
        }
    }

    string write_to_output;
    for (size_t i = 0; i < _output.remaining_capacity() && _flag.front(); ++i) {
        write_to_output += _reassembler_buffer.front();
        _reassembler_buffer.pop_front();
        _flag.pop_front();
        _reassembler_buffer.emplace_back('\0');
        _flag.emplace_back(false);
    }

    if (write_to_output.length() > 0) {
        stream_out().write(write_to_output);
        _bytes_unreassembled -= write_to_output.length();
    }

    if (eof) {
        _eof = true;
        _eof_index = push_end;
    }
    if (_eof && _eof_index == _output.bytes_written()) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _bytes_unreassembled; }

bool StreamReassembler::empty() const { return _bytes_unreassembled == 0; }
