#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {  return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) { 
    if(!_active) return;

    _time_since_last_segment_received = 0;

    // Listen ---- receive a segment with SYN and Seqno 
    // LISTEN ---> SYN RECEIVED ---- step 2 of 3-way-handshake
    // TCPState::LISTEN  ---- TCPReceiver::LISTEN && TCPSender::CLOSED
    if(_sender.next_seqno_absolute() == 0 && ! _receiver.ackno().has_value()){
        if(seg.header().syn){
            // receiver update it's ISN and push data into byte_stream
            // it's ackno() has value
            _receiver.segment_received(seg);
            // sender's next absolute seqno == 1
            connect();
        }
        return;
    }

    // SYN SENT ---- step 3 of 3-way-handshake
    // TCPState::SYN_SENT  ---- TCPReceiver::LISTEN && TCPSender::SYN_SENT
    if(_sender.next_seqno_absolute() != 0 && _sender.bytes_in_flight() == _sender.next_seqno_absolute()
        && !_receiver.ackno().has_value()){
            // data segments with acceptable ACKs should be ignored in SYN_SENT
            if(seg.payload().size() > 0) return;
            if(!seg.header().ack){
                if(seg.header().syn){
                    // simultaneous open
                    _receiver.segment_received(seg);
                    _sender.send_empty_segment();
                }
                return;
            }
        }

    // if the rst (reset) flag is set, sets both the inbound and outbound streams to the error
    if(seg.header().rst){
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        return;
    }

    // ESTABLISHED
    _receiver.segment_received(seg);
    // if ACK flag is set,tells the sender: ackno, window_size
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }

    // if the incoming segment occupied any sequence numbers
    // at least one segment is sent in reply
    if(seg.length_in_sequence_space() >= 1 && _sender.segments_out().size() == 0){
        _sender.send_empty_segment();
    }

    // responding to a 'keep-alive' segment
    if(_receiver.ackno().has_value() && seg.length_in_sequence_space() == 0 
        && seg.header().seqno == _receiver.ackno().value() - 1){
        _sender.send_empty_segment();
    }
    // after update the ackno and winsize, sender read data from bytestream
    //_sender.fill_window();
    send_whole_tcpsegment();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
   size_t write_size = _sender.stream_in().write(data);
   _sender.fill_window();
   send_whole_tcpsegment();
   std::cout<<"debug ---- "<<"write size: "<<write_size<<" ----"<<std::endl;
   return write_size;
}

// send a tcpsegment with seqno,SYN,payload,FIN,ackno,window_size
void TCPConnection::send_whole_tcpsegment(){
    while(!_sender.segments_out().empty()){
        TCPSegment tcpsegment = _sender.segments_out().front();
        _sender.segments_out().pop();

        if(_receiver.ackno().has_value()){
            tcpsegment.header().ack = true;
            tcpsegment.header().ackno = _receiver.ackno().value();
            tcpsegment.header().win = _receiver.window_size();
        }
        std::cout<<"debug ---- A="<<tcpsegment.header().ack<<",R="<<tcpsegment.header().rst<<" S="<<tcpsegment.header().syn
            <<" F="<<tcpsegment.header().fin<<" seqno: "<<tcpsegment.header().seqno<<" payload().size()="<<tcpsegment.payload().size()<<" ----"<<std::endl;
        _segments_out.push(tcpsegment);
    }

    
    clean_shutdown();
}

//! clean shutdown ---- get to "done" without error
//! four prerequisites to having a clean shutdown
//! 1. The inbound stream has been fully assembled and has ended.
//! 2. The outbound stream has been ended by the local application and fully sent 
//! 3. The outbound stream has been fully acknowledged by the remote peer
//! 4. The local TCPConnection is confident that the remote peer can satisfy prereq#3
//! -option A: lingering after both streams end.he remote peer doesn¡¯t seem to be retransmitting anything, and the local peer has waited a while to make sure.
//! -option B: passive close.  the remote peer was the first one to end its stream.
void TCPConnection::clean_shutdown(){
    // TCP 4´Î»ÓÊÖ
    // Prereq #1: the inbound stream has been fully assembled and has ended
    bool prereq1 =  _receiver.stream_out().input_ended();

    // Prereq #2:  the outbound stream has been fully ended by the local application and fully sent
    // in byte_stream.cc :  eof = _input_end && _msg_queue.empty()
    bool prereq2 = _sender.stream_in().eof();

    // Prereq #3: the outbound stream has been fully acknowledgeed by the remote peer
    // in prerequisites 1 and 2,when bytes_in_flight() == 0
    bool prereq3 = _sender.bytes_in_flight() == 0;

    if(prereq1 && !prereq2){
        // when outbound reach it's EOF,
        _linger_after_streams_finish = false;
    }

    if(prereq1 && prereq2 && prereq3){
        // option A: lingering after both streams end.
        // option B: passive close
        if(time_since_last_segment_received() >= 10 * _cfg.rt_timeout || !_linger_after_streams_finish){
            _active = false;
        }
    }
}

//! unclean shutdown
//! TCPConnection either sends or receives a segment with the rst flag set
void TCPConnection::unclean_shutdown(){
    // outbound ByteStream set in error state
    _sender.stream_in().set_error();
    // inbound ByteStream set in error state
    _receiver.stream_out().set_error();
    _active = false;
    TCPSegment tcpsegment;
    tcpsegment.header().rst = true;
    if(_receiver.ackno().has_value()){
        tcpsegment.header().ack = true;
        tcpsegment.header().ackno = _receiver.ackno().value();
    }
    _segments_out.push(tcpsegment);
    //send_whole_tcpsegment();
}


//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    if(!_active) return;
     _sender.tick(ms_since_last_tick);
    _time_since_last_segment_received += ms_since_last_tick;
    if(_sender.consecutive_retransmissions() >  _cfg.MAX_RETX_ATTEMPTS){
        unclean_shutdown();
        return;
    }
    send_whole_tcpsegment();
    clean_shutdown();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_whole_tcpsegment();
}

void TCPConnection::connect() {
    //! call _sender.fill_window()
    _sender.fill_window();
    send_whole_tcpsegment();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            unclean_shutdown();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
