#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <thread>
#include <random>
#include <functional>
#include <cassert>

#include <mutex>
#include <condition_variable>
#include <cstring>
#include <atomic>
#include <array>

using namespace std;

namespace {

constexpr uint64_t MAX_SLEEP_NS = 100000;

/////////////////////////////////////////////////////////////////////////////////
//mutex mtx_g;
//condition_variable cv_in;
//condition_variable cv_out;
//uint8_t data_g[256];
//bool consumed_g = true;
std::atomic<int> running_count_g(0);


class msg_queue {
 public:
    msg_queue(unsigned queue_size);

    void publish(const uint8_t*);
    void on_produce_finished();

    const uint8_t* get_unread_msg() const;
    void move_to_next_unread_msg();
 
 private:
    using unique_lock_t = unique_lock<std::mutex>;

    std::vector<array<uint8_t ,256>> msgs_;
    size_t queue_size_;
    alignas(64) size_t next_for_publishing_;
    alignas(64) size_t last_read_;
    mutable mutex mt_;
    mutable condition_variable cv_;
    
    size_t get_next_index(size_t curr_value) const;
};

msg_queue msg_queue_g(1000);

msg_queue::msg_queue(unsigned queue_size) : queue_size_(queue_size), 
    next_for_publishing_(0U), last_read_(queue_size_-1) {
    static_assert(queue_size>0)
    msgs_.resize(queue_size);
}

size_t msg_queue::get_next_index(size_t curr_value) const {
    return curr_value == queue_size_ ? 0 : curr_value + 1;
}

void msg_queue::publish(const uint8_t* data) {
    size_t nbytes = *data;

    unique_lock_t lk(mt_);
    while (next_for_publishing_ == get_next_index(last_read_)) {
        cv_.wait(lk);
    }
    uint8_t* msg_ptr = &msgs_[next_for_publishing_][0];
    next_for_publishing_ = get_next_index(next_for_publishing_);
    lk.unlock();
    memcpy(msg_ptr, data, nbytes + 1);
}

void msg_queue::on_produce_finished() {
    cv_.notify_one();
}

const uint8_t* msg_queue::get_unread_msg() const {
    unique_lock_t lk(mt_);
    while (next_for_reading_ == next_for_publishing_ && running_count_g > 0) {
        cv_.wait(lk);
    }

    if (next_for_reading_ != next_for_reading_) {
        const uint8_t* msg_ptr = &msgs_[next_for_reading_][0];
        return msg_ptr;
    }

    return nullptr;
}

void msg_queue::move_to_next_unread_msg() {
    unique_lock_t lk(mt_);
    while (next_for_reading_ == next_for_publishing_ && running_count_g > 0) {
        cv_.wait(lk);
    }
    if (next_for_reading_ != next_for_reading_) {
        ++next_for_reading_;
        if (next_for_reading_ == queue_size_) {
            next_for_reading_ = 0;
        }
    } 
}


/////////////////////////////////////////////////////////////////////////////////

typedef std::function<const uint8_t*()> records_f;
void produce(records_f);

class Producer {
    uint8_t buf_[256];
    uint64_t nrecords_;
    thread worker_;
    bool good_ = true;
    default_random_engine random_gen_;

    static uniform_int_distribution<uint64_t> ud_;

  public:
    Producer(int seed) : random_gen_(seed) {}

    Producer(Producer&& other) : nrecords_(other.nrecords_), worker_(move(other.worker_)), random_gen_(other.random_gen_)
    {
        memcpy(buf_, other.buf_, sizeof(buf_) / sizeof(buf_[0]));
        other.good_ = false;
    }

    ~Producer() {
        if (good_) {
            worker_.join();
        }
    }

    void start(unsigned long max_records) {
        nrecords_ = ud_(random_gen_) % max_records;
        cerr << "Starting producer for " << nrecords_ << " records." << endl;
        ++running_count_g;
        records_f f = std::bind(&Producer::next_record, this);
        worker_ = thread(std::bind(produce, f));
    }

  private:
    const uint8_t* next_record() {
        if (0 == nrecords_) {
            return nullptr;
        }

        --nrecords_;

        uint64_t r = ud_(random_gen_);

        buf_[0] = r;
        unsigned int i = 1;
        for (auto* p = buf_ + 1; buf_ + buf_[0] >= p; ++p) {
            if (i >= sizeof(r)) {
                r = ud_(random_gen_);
                i = 0;
            }
            *p = reinterpret_cast<uint8_t*>(&r)[i];
        }

        uint64_t sleep_ns = ud_(random_gen_) % MAX_SLEEP_NS;
        this_thread::sleep_for(chrono::nanoseconds(sleep_ns));

        return buf_;
    }
};
uniform_int_distribution<uint64_t> Producer::ud_;

void produce(records_f next_record) {
    while (true) {
        const uint8_t* data = next_record();
        /////////////////////////////////////////////////////////////////////////////////
        if (data) {
            msg_queue_g.publish(data);
        } else {
            --running_count_g;
        }
        /////////////////////////////////////////////////////////////////////////////////
    }
    msg_queue_g.on_produce_finished();
}

typedef tuple<uint8_t, uint64_t> result_t;

result_t
consume()
{
    uint8_t acc = 0;
    uint64_t cnt = 0;

    while (true) {
        /////////////////////////////////////////////////////////////////////////////////
        const uint8_t* msg_ptr = msg_queue_g.get_unread_msg();
        if (msg_ptr) {
            ++cnt;
            for (unsigned i = 0; i < msg_ptr[0]; ++i) {
                acc ^= msg_ptr[i + 1];
            }
            msg_queue_g.move_to_next_unread_msg();
        } else {
            if (0 == running_count_g) {
                break;
            }
        }
        /////////////////////////////////////////////////////////////////////////////////
    }

    return make_tuple(acc, cnt);
}

}

int
main(int argc, char** argv)
{
    if (argc < 4) {
        cerr << "Usage: " << argv[0] << " <seed> <N_threads> <max_records>" << endl;
        exit(1);
    }

    unsigned int seed = stoi(argv[1]);
    int n = stoi(argv[2]);
    unsigned long M = stol(argv[3]);

    default_random_engine reng(seed);
    uniform_int_distribution<int> ud;

    vector<Producer> producers;
    for (int i = 0; i < n; ++i) {
        producers.push_back(Producer(ud(reng)));
    }

    for (auto&p : producers) {
        p.start(M);
    }

    auto ts1 = chrono::high_resolution_clock::now();
    auto result = consume();
    double d = chrono::duration<double>(chrono::high_resolution_clock::now() - ts1).count();

    cerr << "Result: " << unsigned(get<0>(result)) << " from " << get<1>(result) << " records in " << d << " seconds." << endl;

    return 0;
}