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

    void resize(unsigned queue_size);
    void publish(const uint8_t*);
    void on_produce_finished();
    bool read_msg(array<uint8_t ,256>&);
 
 private:
    using unique_lock_t = unique_lock<std::mutex>;

    std::vector<array<uint8_t ,256>> msgs_;
    size_t queue_size_;
    alignas(64) size_t last_published_;
    alignas(64) size_t last_read_;
    alignas(64) bool published_is_ahead_;
    mutable mutex mt_;
    mutable condition_variable cv_;
    
    size_t get_next_index(size_t curr_value) const;
};

msg_queue msg_queue_g(2);

msg_queue::msg_queue(unsigned queue_size) :  
	last_published_(std::numeric_limits<size_t>::max()),
	last_read_(std::numeric_limits<size_t>::max()) {
    if (queue_size <= 1U) {
        queue_size += 1;
    }
    queue_size_ = queue_size;
    msgs_.resize(queue_size_);
    published_is_ahead_ = true;
}

void msg_queue::resize(unsigned queue_size) {
	// not thread-safe
	last_published_ = std::numeric_limits<size_t>::max();
	last_read_ = std::numeric_limits<size_t>::max();
    if (queue_size <= 1U) {
        queue_size += 1;
    }
    queue_size_ = queue_size;
    msgs_.resize(queue_size_);
    published_is_ahead_ = true;
}

size_t msg_queue::get_next_index(size_t curr_value) const {
    return curr_value == queue_size_ ? 0 : curr_value + 1;
}

void msg_queue::publish(const uint8_t* data) {
    size_t nbytes = *data;

    unique_lock_t lk(mt_);
    while (true) {
    	if (last_published_ != std::numeric_limits<size_t>::max()) {
    		if (last_read_ != std::numeric_limits<size_t>::max()) {
    			if (published_is_ahead_) {
    				last_published_ = get_next_index(last_published_);
    				if (last_published_ == 0U) {
    					published_is_ahead_ = false;
    				}
    				break;
    			} else {
        			if (last_published_ != last_read_) {
        				break;
        			} else {
        				// have to wait for reading buffer #[last_read_+1]
        			}
    			}
    		} else {
    			if (last_published_ != queue_size_-1) {
    				last_published_ = get_next_index(last_published_);
    				break;
    			} else {
    				// have to wait for reading buffer #0
    			}
    		}
    	} else {
    		last_published_ = 0U;
    		break;
    	}
        cv_.wait(lk);
    }
    uint8_t* msg_ptr = &msgs_[last_published_][0];
    memcpy(msg_ptr, data, nbytes + 1);
    lk.unlock();
    cv_.notify_one();
}

void msg_queue::on_produce_finished() {
    cv_.notify_one();
}

bool msg_queue::read_msg(array<uint8_t ,256>& msg) {
    unique_lock_t lk(mt_);
    while (true) {
    	if (last_published_ != std::numeric_limits<size_t>::max()) {
    		if (last_read_ != std::numeric_limits<size_t>::max()) {
    			if (last_read_ != last_published_) {
    				last_read_ = get_next_index(last_read_);
    				if (last_read_ == 0U) {
    					published_is_ahead_ = true;
    				}
    				break;
    			} else {
    				if (running_count_g == 0) {
    					return false;
    				} else {
    					// have to wait for publishing buffer #[last_published_+1]
    				}
    			}
    		} else {
    			last_read_ = 0U;
				break;
    		}
    	} else {
    		if (running_count_g == 0) {
    			return false;
    		} else {
    			// have to wait for first publishing
    		}
    	}
        cv_.wait(lk);
    }
    msg =  msgs_[last_read_];
    lk.unlock();
    cv_.notify_one();
	return true;
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
            break;
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

    array<uint8_t ,256> msg;
    while (true) {
        /////////////////////////////////////////////////////////////////////////////////
        auto read_res = msg_queue_g.read_msg(msg);
        if (!read_res) {
        	break;
        }
		++cnt;
		auto len = msg[0];
		for (unsigned i = 0, j = 1; i < len; ++i, ++j) {
			acc ^= msg[j];
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

    msg_queue_g.resize(100);

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
