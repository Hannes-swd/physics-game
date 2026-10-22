#include "Jobs.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

namespace jobs {
namespace {

class Pool {
public:
    Pool() {
        const size_t n = std::clamp<size_t>(std::thread::hardware_concurrency() / 2, 2, 8);
        for (size_t i = 0; i < n; ++i)
            workers_.emplace_back([this] {
                // unter der Priorität des Hauptthreads: Bild und Eingabe gehen vor
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                run();
            });
    }
    ~Pool() {
        {
            std::lock_guard<std::mutex> l(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
    void submit(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> l(m_);
            q_.push_back(std::move(f));
        }
        cv_.notify_one();
    }
    // vor die Hintergrundarbeit: kurze Teilaufgaben einer parallelFor-Schleife
    void submitFront(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> l(m_);
            q_.push_front(std::move(f));
        }
        cv_.notify_one();
    }
    size_t size() const { return workers_.size(); }
    size_t queued() {
        std::lock_guard<std::mutex> l(m_);
        return q_.size();
    }

private:
    void run() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [&] { return stop_ || !q_.empty(); });
                if (stop_ && q_.empty()) return;
                f = std::move(q_.front());
                q_.pop_front();
            }
            f();
        }
    }
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> q_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
};

Pool& pool() {
    static Pool p;
    return p;
}

} // namespace

void submit(std::function<void()> task) { pool().submit(std::move(task)); }

size_t threads() { return pool().size(); }
size_t queued() { return pool().queued(); }

void parallelFor(size_t count, const std::function<void(size_t)>& fn) {
    if (count == 0) return;
    if (count == 1) {
        fn(0);
        return;
    }
    struct Shared {
        std::atomic<size_t> next{0}, done{0};
        size_t count = 0;
        const std::function<void(size_t)>* fn = nullptr;
    };
    auto s = std::make_shared<Shared>();
    s->count = count;
    s->fn = &fn;  // gültig, solange parallelFor wartet (bis alle Einträge erledigt sind)
    auto work = [s] {
        for (size_t i; (i = s->next.fetch_add(1)) < s->count;) {
            (*s->fn)(i);
            s->done.fetch_add(1);
        }
    };
    const size_t helpers = std::min(pool().size(), count - 1);
    for (size_t h = 0; h < helpers; ++h) pool().submitFront(work);
    work();
    // die letzten Einträge rechnen vielleicht noch Hilfs-Threads
    while (s->done.load() < count) std::this_thread::yield();
}

} // namespace jobs
