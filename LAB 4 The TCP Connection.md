# LAB 4 TCPConnection

in Lab 4, you will make the overarching module, called TCPConnection, that combines your TCPSender and TCPReceiver and handles the global housekeeping for the connection.

the TCPConnection mostly just combines the sender and receiver modules that you have implemented in the earlier labs—the TCPConnection itself can be implemented in less than 100 lines of code

![image-20220228153845876](LAB 4 The TCP Connection.assets/image-20220228153845876.png)

## 1. The TCP connection

![image-20220228154652235](LAB 4 The TCP Connection.assets/image-20220228154652235.png)

TCPConnection负责将TCPSender和TCPReceiver结合，负责发送和接收segments

### Receiving segments

TCPConnection receives TCPSegments from the Internet when its `segment_received` method is called. When this happens, the TCPConnection looks at the segment and:

- if the **RST (reset) flag** is set, sets both the inbound and outbound streams to the error state and kills the connection permanently. 

- gives the segment to the TCPReceiver so it can inspect the fields it cares about on incoming segments: **seqno, SYN , payload, and FIN**

- if the **ACK** flag is set, tells the TCPSender about the fields it cares about on incoming segments: **ackno and window size**

- if the incoming segment occupied any sequence numbers, the TCPConnection makes sure that at least one segment is sent in reply, to reflect an update in the **ackno and window size**.

- There is one extra special case that you will have to handle in the TCPConnection’s `segment_received()` method: responding to a **“keep-alive” segment**. The peer may choose to send a segment with an invalid sequence number to see if your TCP implementation is still alive (and if so, what your current window is). Your TCPConnection should reply to these “keep-alives” even though they do not occupy any sequence numbers. Code to implement this can look like this:

  ```
  if (_receiver.ackno().has_value() and (seg.length_in_sequence_space() == 0)
  	and seg.header().seqno == _receiver.ackno().value() - 1) {
  	_sender.send_empty_segment();
  }
  ```

### Sending segments

The TCPConnection will send TCPSegments over the Internet:

- any time **the TCPSender has pushed a segment onto its outgoing queue**, having set the fields it’s responsible for on outgoing segments: (**seqno, syn , payload, and fin** )
- **abort the connection**, and **send a reset segment** to the peer (**an empty segment with the rst flag set**), if the number of consecutive retransmissions is more than an upper limit TCPConfig::MAX RETX ATTEMPTS.
- end the connection cleanly if necessary.

![image-20220228160410688](LAB 4 The TCP Connection.assets/image-20220228160410688.png)



![image-20220302091417413](LAB 4 The TCP Connection.assets/image-20220302091417413.png)



![image-20220307095215315](LAB 4 The TCP Connection.assets/image-20220307095215315.png)

![image-20220307095220993](LAB 4 The TCP Connection.assets/image-20220307095220993.png)

























