# LAB 3: The TCP Sender

![image-20220222121023954](C:\Users\pb\AppData\Roaming\Typora\typora-user-images\image-20220222121023954.png)

TCPSender需要做的：

- 通过处理接收TCPReceiver的TCPSegment跟踪Receiver的window
- 通过从ByteStream读取数据作为payload尽可能地去填充Receiver的window，创建新的TCPSegment（必要时填充SYN或FIN标志位）发送给Receiver，直到window填满或ByteStream为空
- 跟踪TCPSender已经发出但没有被TCPReceiver接收到（通过ackno判断）的“outstanding” Segment
- 实现TCP的ARQ（automatic repeat request）功能，当发送一个TCP Segment的时间超过一定限制时重发，最终实现TCPReceiver至少能接收到每一个数据一次

## 3.1 TCPSender如何判断一个TCPSegment丢失？

TCPSender会发送许多的TCPSegment，其中的每一个都可能包含ByteStream的子串，并通过Sequence Number记录子串在ByteStream中的位置。the SYN flag at the beginning of the stream, and FIN flag at the end.

除发送这些TCPSegment之外，TCPSender同样需要追踪“outstanding” Segment直到这些TCPSegment所占据的所有sequence numbers全都被接收到（通过TCPReceiver发送的ackno判断）。

`tcp_sender.hh --- tick()`

![image-20220222123614501](C:\Users\pb\AppData\Roaming\Typora\typora-user-images\image-20220222123614501.png)

`tcp_sender.cc --- tick()`

![image-20220222123810796](C:\Users\pb\AppData\Roaming\Typora\typora-user-images\image-20220222123810796.png)

TCPSender会定期调用tick方法得到距离上一次tick的时间，ms_since_last_tick.

 TCPSender负责收集得到`“outstanding” Segments`，并判断其中最先发送的segment是否成为了“outstanding” segment（**`too long without acknowledgment，without all of its sequence numbers being acknowledged`**），当发生这种情况时，此片段需要重发。

> why am i doing this ?
>
> 最终的目标是在限定时间内让sender得知segments丢失的情况和segments需要重发的情况。
>
> 在segments重发之前等待时间的设置很重要：
>
> - 不能让sender等待过长的时间去重发一个segment --- because that delays the bytes flowing to the receiving application
> - 不能将该时间设定得太短，重复发送浪费网络带宽

**`outstanding for too long的规则：`**

1. 每隔几毫秒，*void* TCPSender**::**tick(*const* *size_t* **ms_since_last_tick**)将会被调用，其中ms_since_last_tick表示自从上一次调用tick()过去了多少ms。通过它来维护TCPSender已经存活的时间。`不要通过调用系统或cpu的 time 或 clock函数，tick函数是获取消息时间的唯一函数`

2. TCPSender接收一个initial value用于初始化*`_initial_retransmission_timeout`*，**`retransmission timeout(RTO)`**是重传一个TCP segment之前等待的时间，它的值会随着时间改变。

   ![image-20220222224539656](LAB 3 The TCP Sender.assets/image-20220222224539656.png)

   ![image-20220222224408879](LAB 3 The TCP Sender.assets/image-20220222224408879.png)

3. 需要实现一个`retransmission timer`：一个在特定时间开始的alarm，一旦RTO的时间过去便关闭。

4. 每当发送出一个包含数据的segment，让timer运行起来，以便在RTO过后expire

   By “expire,” we mean that the time will run out a certain number of milliseconds in
   the future.

5. 当所有的`outstanding data`都被acknowledged，停止retransmission timer

6. 当tick被调用且 retransmission timer expired时：

   - a.  重传没有被TCPReceiver完全接收的earliest（lowest sequence number) segment，需要在TCPSender中通过内部的数据结构存储`outstanding data`
   - b.  window size is nonzero
     - i.  追踪连续重传的数目，并在重传segment之后增加它的数目。TCPConnection会由此判断连接是否hopeless，是否需要中止该连接
     - ii.  将RTO的值翻倍x2，这就是指数退避（exponential backoff），通过减缓重传避免对糟糕的网络进行进一步的破化
   - c.  对timer进行重置以便在RTO过后expire

7. 当receiver传递给sender一个ackno表示已经成功接收到新的数据之后（ackno对应的absolute seqence number比之前的ackno大）

   - 将RTO设置为initial value
   - 当sender含有任何的outstanding data，重启retransmission timer
   - 重置`consecutive retransmissions`为0

最好将retransmission timer的功能实现放在单独一个类文件中。

## 3.2   Implementing the TCP sender

TCP sender所需实现的基本功能：将给定的ByteStrea划分到各个segment并发送给TCP receiver，当没有及时得到segment的反馈时进行重发。

如下是本次实验所需实现的4个接口：

1. `void fill_window()`

   TCPSender从它的input ByteStream中读取数据，并将尽可能多的数据放到TCPSegment中，as long as there are new bytes to be read and space available in the window.

   每次发送的TCP Segment都需要尽可能的fits fully，但不能超TCPConfig::*MAX_PAYLOAD_SIZE* **=** 1452

   可以通过 TCPSegment::length in sequence space()计算被一个segment占据的sequence number个数。

   ![image-20220222233312339](LAB 3 The TCP Sender.assets/image-20220222233312339.png)

   > What should I do if the window size is zero? 
   >
   > 当receiver已经申明其window size为0，fill_window()应该表现为window size为1的情形。当sender发送单个字节数据的segment，receiver会拒绝接收，但这样同样可以唤起receiver发送一个新的acknowledge segment，当receiver中的window size不为0时可继续发送数据。如若不然，sender永远不会得知windows size不再为0且可继续发送数据。

2. `void ack_received( const WrappingInt32 ackno, const uint16_t window_size)`

   接收到receiver的segment，其中传达了其window的new left(= ackno)，new right(= ackno + window size)，TCPSender需要查看outstanding segments，将其中已经完全接收（the ackno is greater than all of the sequence numbers in the segment）的移除。TCPSender同样需要在window size有空闲时发送数据。

3. `void tick( const size t ms since last tick )`

   Time has passed — a certain number of milliseconds since the last time this method
   was called. 

4. `void send_empty_segment()`

   TCPSender需要发出一个在占据 0 seqno的TCPSement，其中的sequence number必须设置正确，可作为一个ACK segment

   Note: a segment like this one, which occupies no sequence numbers, doesn’t need to be
   kept track of as “outstanding” and won’t ever be retransmitted.

## 3.3   Theory of testing

![image-20220223113109624](LAB 3 The TCP Sender.assets/image-20220223113109624.png)

TCPSender中已经提供了公共的接口来定义数据流发送过程中的各个状态。

## 3.4   FAQs and special cases

- How do I “send” a segment ?

  将其放入_segments_out queue，对于TCPSender而言，将其放入队列后便视为发出。之后通过segments_out()发送到receiver。

  ![image-20220223113438358](LAB 3 The TCP Sender.assets/image-20220223113438358.png)

  ![image-20220223113731418](LAB 3 The TCP Sender.assets/image-20220223113731418.png)

- how do I both “send” a segment and also keep track of that same segment as
  being outstanding, so I know what to retransmit later? Don’t I have to make a copy of
  each segment then? Is that wasteful?

  当发出一个包含数据的segment时，可能会想到将其push到_segment_out queue中，并创建一个内部的数据结构存储segment的副本来追踪outstanding segments。事实证明这不是一件很浪费的事情，segment中的payload以一个只读通过引用计算存储的buffer，不会对实际的payload data进行复制。

- What should my TCPSender assume as the receiver’s window size before I’ve gotten an
  ACK from the receiver ?

  one byte.

- What do I do if an acknowledgment only partially acknowledges some outstanding segment? Should I try to clip off the bytes that got acknowledged?

  只有当一个outstanding segment中的所有数据被接收时才将其移除，部分接收不影响。

- If I send three individual segments containing “a,” “b,” and “c,” and they never get
  acknowledged, can I later retransmit them in one big segment that contains “abc”? Or
  do I have to retransmit each segment individually?

  TCPSender可以将其合并，但没有必要，只需要单独追踪各个segment即可。

- Should I store empty segments in my “outstanding” data structure and retransmit them
  when necessary?

  一个不包含sequence number的segment不需要重传，更不需要存储在内部的数据结构中，如ACK segment

## 具体实现

`定时器class timer`

`tcp_timer.hh`

```
class TCPTimer {
  private:
    // 当前TCPTimer是否运行
    bool _timer_run;
    // 记录TCPTimer运行的时间
    unsigned int _retransmission_timer;
    // RTO
    unsigned int _retransmission_timeout;

  public:
    TCPTimer(const unsigned int retransmission_timeout);
    void tick(const unsigned int tick);
    bool timeout();
    void reset();
    void double_RTO();
    void reset_RTO(const unsigned int retransmission_timeout);
    void stop();
    bool running() const;
    void start();
};
```

`tcp_timer.cc`

```
#include "tcp_timer.hh"

using namespace std;

TCPTimer::TCPTimer(const unsigned int retransmission_timeout)
    : _timer_run(false), _retransmission_timer(0), _retransmission_timeout(retransmission_timeout) {}

void TCPTimer::tick(const unsigned int tick) { _retransmission_timer += tick; }

// 定时器是否超时
bool TCPTimer::timeout() { return _retransmission_timer >= _retransmission_timeout; }

// 重置timer
// 即将timer置位为0
void TCPTimer::reset() { _retransmission_timer = 0; }

//
void TCPTimer::double_RTO() { _retransmission_timeout *= 2; }

void TCPTimer::reset_RTO(const unsigned int retransmission_timeout) {
    _retransmission_timeout = retransmission_timeout;
}

void TCPTimer::stop() { _timer_run = false; }

bool TCPTimer::running() const { return _timer_run; }

void TCPTimer::start() {
    _retransmission_timer = 0;
    _timer_run = true;
}
```

`TCPSender::fill_window()`

```
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
    uint64_t recv_remain_winsize = recv_winsize > bytes_in_flight() ?  recv_winsize - bytes_in_flight():0;
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
        if(tcpsegment.header().fin) return;
    }
}
```

`void TCPSender::ack_received()`

```
//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 得到当前接收数据包的absolute seqno
    uint64_t abs_ackseqno = unwrap(ackno, _isn, _recv_ackno);

    // abs_ackseqno <= _recv_ackno ---- 此ackno之前的所有已经确认
    // abs_ackseqno > _next_seqno ---- 无效的ackno
    if (abs_ackseqno <= _recv_ackno || abs_ackseqno > next_seqno_absolute()){
        //  t_send_window.cc中有一只更新window size的测试
        if(abs_ackseqno == _recv_ackno) _recv_winsize = window_size;
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

    return;
}
```

`void TCPSender::tick()`

```
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
```

`void TCPSender::send_empty_segment()`

```
void TCPSender::send_empty_segment() {
    TCPSegment tcpsegment;
    tcpsegment.header().seqno = next_seqno();
    // empty segment不占用序号,不需要进行重传
    _segments_out.push(tcpsegment);
}
```

