#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{std::random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _tcptimer(retx_timeout) {}

// 发送一个TCP报文包
void TCPSender::send_tcpsegment(TCPSegment &seg) {
    seg.header().seqno = next_seqno();
    // 将segment推入_segments_out视为发出
    _segments_out.push(seg);
    // 每发出一个segment,内部记录
    _segments_outstanding.push(seg);
    _next_seqno += static_cast<uint64_t>(seg.length_in_sequence_space());

    // 每当发出一个包含数据的segement之后便让timer运行起来
    // 以便在RTO时间后expires
    if (!_tcptimer.running()) {
        _tcptimer.reset();
        _tcptimer.start();
    }
}

void TCPSender::fill_window() {
    // FIN_SENT OR FIN_ACKED
    if (stream_in().eof() && next_seqno_absolute() == stream_in().bytes_written() + 2)
        return;

    TCPSegment tcpsegment;
    // send a syn segment for handshake
    // CLOSED STATE --- waitting for stream to begin(no SYN sent)
    // 发送SYN时不能携带任何数据
    if (next_seqno_absolute() == 0) {
        tcpsegment.header().syn = true;
        send_tcpsegment(tcpsegment);
        return;
    }

    // SYN_SENT STATE ---- stream started but nothing acknowledged
    if (!_segments_outstanding.empty() && _segments_outstanding.front().header().syn)
        return;

    // 1. 尚未收到receiver的ACK,设_recv_winsize初始为0
    // 2. 收到receiver的ACK得知窗口大小为0
    // 当接收方窗口大小为0时,fill_window()函数将窗口大小视为1
    uint64_t recv_winsize = _recv_winsize > 0 ? _recv_winsize : 1;

    // 剩下可发送数据大小 = 窗口大小 - 已发送但尚未接收字节数
    // !!!!!!!!!! 此处存在越界的可能  !!!!!!!!!!
    uint64_t recv_remain_winsize = recv_winsize > bytes_in_flight() ? recv_winsize - bytes_in_flight() : 0;
    // receiver尚且有剩余的窗口可以接收数据
    while (recv_remain_winsize > 0) {
        // TCPSender从它的input ByteStream中读取数据
        // 并将尽可能多的数据放到TCPSegment中
        uint64_t read_size = std::min(static_cast<uint64_t>(recv_remain_winsize), TCPConfig::MAX_PAYLOAD_SIZE);
        tcpsegment.payload() = stream_in().read(read_size);

        // 有足够的空间容纳fin
        // syn和fin都占据一个窗口大小
        if (stream_in().eof() && tcpsegment.length_in_sequence_space() < recv_remain_winsize) {
            tcpsegment.header().fin = true;
        }

        // empty segment
        if (tcpsegment.length_in_sequence_space() == 0)
            return;
        recv_remain_winsize -= tcpsegment.length_in_sequence_space();
        send_tcpsegment(tcpsegment);
        if (tcpsegment.header().fin)
            return;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 得到当前接收数据包的absolute seqno
    uint64_t abs_ackseqno = unwrap(ackno, _isn, _recv_ackno);

    // abs_ackseqno <= _recv_ackno ---- 此ackno之前的所有已经确认
    // abs_ackseqno > _next_seqno ---- 无效的ackno
    if (abs_ackseqno <= _recv_ackno || abs_ackseqno > next_seqno_absolute()) {
        //  t_send_window.cc中有一只更新window size的测试
        if (abs_ackseqno == _recv_ackno)
            _recv_winsize = window_size;
        return;
    }

    // ---- 以下是接收到new data所需采取的操作 -----
    // 更新receiver的接收窗口大小
    _recv_winsize = window_size;

    // the ackno reflects an absolute sequence number bigger than any previous ackno
    // a. set the RTO back to its “initial value.”
    _tcptimer.reset_RTO(_initial_retransmission_timeout);

    // 更新已经接收的seqno
    _recv_ackno = abs_ackseqno;

    TCPSegment tcpsegment;
    // 对outstanding segments进行检测
    // 对已经完全接收到的segemnts进行移除
    while (!_segments_outstanding.empty()) {
        tcpsegment = _segments_outstanding.front();

        // fully accpeted
        // the ackno is greater than all of the sequence numbers in the segment
        if (unwrap(tcpsegment.header().seqno, _isn, _recv_ackno) + tcpsegment.length_in_sequence_space() <=
            abs_ackseqno) {
            _segments_outstanding.pop();
        } else {
            break;
        }
    }

    if (_segments_outstanding.empty()) {
        _tcptimer.stop();
    } else {
        // b. restart the retransmission timer
        _tcptimer.reset();
    }
    // 3. reset the count of “consecutive retransmissions” back to zero.
    _consecutive_retransmissions = 0;
    fill_window();
    return;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_tcptimer.running())
        return;
    // tcptimer中的定时器加上ms_since_last_tick
    // 时间过去ms_since_last_tick
    _tcptimer.tick(ms_since_last_tick);

    if (_tcptimer.timeout() && !_segments_outstanding.empty()) {
        // 当tick被调用且retransmission timer expired时
        // a. 重传没有被TCPReceiver完全接收的segments
        _segments_out.push(_segments_outstanding.front());

        // b. window size is nonzero
        // i. 追踪连续重传的数目，并在重传segment之后增加它的数目
        // ii. 将RTO的值翻倍 ---- 指数退避算法
        //
        if (_segments_outstanding.front().header().syn || _recv_winsize != 0) {
            _consecutive_retransmissions++;
            _tcptimer.double_RTO();
        }

        // Every time a segment containing data is sent,if the timer is not running,start it
        _tcptimer.reset();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment tcpsegment;
    tcpsegment.header().seqno = next_seqno();
    // empty segment不占用序号,不需要进行重传
    _segments_out.push(tcpsegment);
}
