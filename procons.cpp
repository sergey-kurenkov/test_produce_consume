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

using namespace std;

namespace {

constexpr uint64_t MAX_SLEEP_NS = 100000;

/////////////////////////////////////////////////////////////////////////////////
mutex mtx_g;
condition_variable cv_in;
condition_variable cv_out;
uint8_t data_g[256];
bool consumed_g = true;
int running_count_g = 0;
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
        unique_lock<mutex> lk(mtx_g);
        cv_in.wait(lk, [] {return consumed_g;});

        if (data) {
            size_t nbytes = *data;
            memcpy(data_g, data, nbytes + 1);
            consumed_g = false;

            cv_out.notify_one();
            lk.unlock();
        } else {
            --running_count_g;
            cv_out.notify_one();
            lk.unlock();
            break;
        }

        /////////////////////////////////////////////////////////////////////////////////
    }
}

typedef tuple<uint8_t, uint64_t> result_t;

result_t
consume()
{
    uint8_t acc = 0;
    uint64_t cnt = 0;

    while (true) {
        /////////////////////////////////////////////////////////////////////////////////
        unique_lock<mutex> lk(mtx_g);
        cv_out.wait(lk);

        if (0 == running_count_g) {
            break;
        }

        if (!consumed_g) {
            ++cnt;
            for (unsigned i = 0; i < data_g[0]; ++i) {
                acc ^= data_g[i + 1];
            }

            consumed_g = true;
        }
        cv_in.notify_one();
        lk.unlock();

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