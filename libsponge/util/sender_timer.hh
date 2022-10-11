#ifndef SPONGE_LIBSPONGE_TIMER_HH
#define SPONGE_LIBSPONGE_TIMER_HH

/*
当发送一个新的segment的时候，如果timer没有开启，那么需要开启timer。
当在RTO内收到一个合法的ACK,有两种情况:
    1.如果sender没发完segments那么需要重启timer,重启的意思是timer从0开始计时。
    2.如果sender已经发完所有的segments了那么需要关闭timer
当超时的情况发生,也是两种情况:
    1.window_size = 0 : 重启timer,重传segments。
    2.window_size != 0 : double RTO, 重启timer,重传segments。
 */
class TCPTimer {
private:
    // 0 refers to the timer not start
    size_t _time_pasted;
    size_t _rto;
    unsigned int _initial_timeout;
    bool _running;
public:

    TCPTimer(unsigned int timeout)
    : _time_pasted(0), _rto(timeout), _initial_timeout(timeout), _running(false){}

    inline void init_rto() {
        _rto = _initial_timeout;
    }

    inline void double_rto() {
        _rto <<= 1;
    }

    inline bool running() {
        return _running;
    }

    inline void start() {
        _time_pasted = 0; // !
        _running = true;
    };

    bool expired(const size_t past) {
        if (!_running) {
            return false;
        }
        _time_pasted += past;
        if (_time_pasted >= _rto) {
            shutdown();
            return true;
        }
        return false;
    }

    void shutdown() {
        _running = false;
        _time_pasted = 0;
    }
};

#endif