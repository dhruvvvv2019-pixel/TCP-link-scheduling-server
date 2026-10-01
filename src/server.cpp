#include "common.hpp"
#include "config.hpp"

struct Request {
    uint64_t id = 0;
    int fd = -1;
    std::string op;
    std::string filename;
    uint64_t bytes = 0;
    uint64_t offset = 0;
    uint64_t rounds = 0;
    uint64_t forfeited = 0;
    uint64_t arrival = 0;
    uint64_t start = 0;
    uint64_t finish = 0;
    uint64_t deficit = 0;
    bool response_sent = false;
    std::ifstream file;
    std::vector<std::string> lines;
    size_t line_index = 0;
    fs::path temp_path;
    std::shared_ptr<std::mutex> file_lock;
    std::unique_ptr<std::unique_lock<std::mutex>> held_lock;
};

class SchedulerServer {
    Config cfg;
    std::string sched;
    uint64_t quantum = 0;
    fs::path root;
    int listen_fd = -1;
    int packet_lines = 1;
    std::string metrics_path;

    std::mutex q_mtx;
    std::condition_variable q_cv;

    std::mutex file_map_mtx;
    std::map<std::string, std::shared_ptr<std::mutex>> file_locks;
    std::deque<std::shared_ptr<Request>> queue;
    std::vector<std::thread> workers;
    std::vector<std::thread> handlers;

    std::mutex all_mtx;
    std::vector<std::shared_ptr<Request>> completed;

    std::atomic<bool> stopping{false};
    std::atomic<uint64_t> next_id{1};
    std::atomic<uint64_t> queue_depth{0};
    std::atomic<uint64_t> a14_count{0};

    static SchedulerServer* instance;

    static void signal_handler(int) {
        if (instance) instance->stopping.store(true);
    }

    std::shared_ptr<std::mutex> file_lock_for(const std::string& name) {
        std::lock_guard<std::mutex> lk(file_map_mtx);

        auto it = file_locks.find(name);
        if (it != file_locks.end()) return it->second;

        auto m = std::make_shared<std::mutex>();
        file_locks[name] = m;
        return m;
    }

    void requeue(const std::shared_ptr<Request>& r) {
        {
            std::lock_guard<std::mutex> lk(q_mtx);
            queue.push_back(r);
            queue_depth.store(queue.size());
        }
        q_cv.notify_one();
    }

    bool read_get_file(Request& r) {
        std::ifstream in(root / r.filename, std::ios::binary);
        if (!in) return false;

        std::string all(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );

        size_t start = 0;

        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i] == '\n') {
                r.lines.push_back(all.substr(start, i - start + 1));
                start = i + 1;
            }
        }

        if (start < all.size()) {
            r.lines.push_back(all.substr(start));
        }

        return true;
    }

    bool send_get_round(Request& r, uint64_t allowance, bool& preempted) {
        uint64_t used = 0;
        preempted = false;

        while (r.line_index < r.lines.size()) {
            const std::string& line = r.lines[r.line_index];
            uint64_t len = line.size();

            if (used > 0 && used + len > allowance) {
                preempted = true;
                break;
            }
            if (used == 0 && len > allowance) {
                if (sched == "drr") {
                 preempted = true;
                 break;
                 }
                if (!send_all(r.fd, line)) return false;

                if (sched == "rr") ++a14_count;

                r.offset += len;
                ++r.line_index;

                preempted = r.line_index < r.lines.size();
                break;
            }

            std::string group;
            int count = 0;
            uint64_t group_bytes = 0;
            size_t idx = r.line_index;

            while (idx < r.lines.size() && count < packet_lines) {
                uint64_t l = r.lines[idx].size();

                if (group_bytes + l > allowance - used) break;

                group += r.lines[idx];
                group_bytes += l;
                ++idx;
                ++count;
            }

            if (group.empty()) {
                preempted = true;
                break;
            }

            if (!send_all(r.fd, group)) return false;

            used += group_bytes;
            r.offset += group_bytes;
            r.line_index = idx;

            if (used >= allowance && r.line_index < r.lines.size()) {
                preempted = true;
                break;
            }
        }

        return true;
    }

    enum class ServeResult {
        DONE,
        REQUEUE,
        FAILED,
        BLOCKED
    };

    ServeResult serve_request(const std::shared_ptr<Request>& r) {
        if (r->start == 0) {
            r->start = mono_ns();
        }

        if (r->op == "GET") {
            if (!r->response_sent) {
                std::string ok = "OK " + std::to_string(r->bytes) + "\n";

                if (!send_all(r->fd, ok)) {
                    return ServeResult::FAILED;
                }

                r->response_sent = true;
            }

            if (sched == "fcfs" || sched == "sjf") {
                bool ignored = false;

                if (!send_get_round(
                        *r,
                        std::numeric_limits<uint64_t>::max(),
                        ignored)) {
                    return ServeResult::FAILED;
                }

                r->offset = r->bytes;
                r->line_index = r->lines.size();

                return ServeResult::DONE;
            }

            uint64_t allowance = quantum;

            if (sched == "drr") {
                r->deficit += quantum;
                allowance = r->deficit;
            }

            bool preempted = false;
            uint64_t before = r->offset;

            if (!send_get_round(*r, allowance, preempted)) {
                return ServeResult::FAILED;
            }

            uint64_t used = r->offset - before;

            if (sched == "rr") {
                if (preempted && used < allowance) {
                    r->forfeited += allowance - used;
                }
            } else if (sched == "drr") {
                r->deficit -= std::min(r->deficit, used);
            }

            return r->offset >= r->bytes
                ? ServeResult::DONE
                : ServeResult::REQUEUE;
        }

        if (!r->response_sent) {
            if (!send_all(r->fd, "OK 0\n")) {
                return ServeResult::FAILED;
            }

            r->response_sent = true;
        }

        if (r->offset > r->bytes) {
            std::cerr
                << "PUT invalid state: offset=" << r->offset
                << " bytes=" << r->bytes
                << std::endl;

            return ServeResult::FAILED;
        }

        uint64_t remaining = r->bytes - r->offset;
        uint64_t allowance = remaining;

        if (sched == "rr") {
            allowance = std::min<uint64_t>(quantum, remaining);
        } else if (sched == "drr") {
            r->deficit += quantum;
            allowance = std::min<uint64_t>(r->deficit, remaining);
        }

        if (allowance == 0) {
    if (r->bytes == 0) {
        fs::path target = root / r->filename;

        r->temp_path =
            root / ("." + r->filename + ".tmp." +
                    std::to_string(r->id));

        std::ofstream out(
            r->temp_path,
            std::ios::binary | std::ios::trunc
        );

        if (!out) {
            return ServeResult::FAILED;
        }

        out.close();

        std::error_code ec;
        fs::rename(
            r->temp_path,
            target,
            ec
        );

        if (ec) {
            return ServeResult::FAILED;
        }

        r->temp_path.clear();
        return ServeResult::DONE;
    }

    return ServeResult::FAILED;
}

        fs::path target = root / r->filename;

if (r->offset == 0) {
    r->temp_path =
        root / ("." + r->filename + ".tmp." +
                std::to_string(r->id));
}

std::ofstream out;

if (r->offset == 0) {
    out.open(
        r->temp_path,
        std::ios::binary | std::ios::trunc
    );
} else {
    out.open(
        r->temp_path,
        std::ios::binary | std::ios::app
    );
}

if (!out) {
    std::cerr
        << "PUT failed to open temporary output file: "
        << r->temp_path
        << std::endl;

    return ServeResult::FAILED;
}

        std::vector<char> buf(8192);
uint64_t got_round = 0;
timeval tv{};
tv.tv_sec = 10;
tv.tv_usec = 0;

::setsockopt(
    r->fd,
    SOL_SOCKET,
    SO_RCVTIMEO,
    &tv,
    sizeof(tv)
);

        while (got_round < allowance) {
            size_t want = static_cast<size_t>(
                std::min<uint64_t>(
                    buf.size(),
                    allowance - got_round
                )
            );

            ssize_t n = ::recv(
                r->fd,
                buf.data(),
                want,
                0
            );

            if (n > 0) {
                out.write(buf.data(), n);

                if (!out) {
                    std::cerr
                        << "PUT file write FAILED: request="
                        << r->id
                        << " offset="
                        << r->offset
                        << std::endl;

                    return ServeResult::FAILED;
                }

                got_round += static_cast<uint64_t>(n);
                r->offset += static_cast<uint64_t>(n);

                continue;
            }

            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n == 0) {
                std::cerr
                    << "PUT recv FAILED: peer closed connection"
                    << " request=" << r->id
                    << " offset=" << r->offset
                    << "/" << r->bytes
                    << std::endl;
            } else {
                int saved_errno = errno;

                std::cerr
                    << "PUT recv FAILED:"
                    << " request=" << r->id
                    << " offset=" << r->offset
                    << "/" << r->bytes
                    << " n=" << n
                    << " errno=" << saved_errno
                    << " (" << std::strerror(saved_errno) << ")"
                    << std::endl;
            }

            return ServeResult::FAILED;
        }

        out.close();

if (!out) {
    std::cerr
        << "PUT output close/flush FAILED:"
        << " request=" << r->id
        << std::endl;

    return ServeResult::FAILED;
}

if (sched == "drr") {
    r->deficit -= std::min(r->deficit, got_round);
}

if (sched == "rr" &&
    r->offset < r->bytes &&
    got_round < allowance) {

    r->forfeited += allowance - got_round;
}
if (r->offset >= r->bytes) {
    std::error_code ec;

    fs::rename(r->temp_path, target, ec);

    if (ec) {
        std::cerr
            << "PUT rename FAILED:"
            << " request=" << r->id
            << " error=" << ec.message()
            << std::endl;

        return ServeResult::FAILED;
    }

    r->temp_path.clear();

    return ServeResult::DONE;
}

return ServeResult::REQUEUE;
    }

    void complete_or_requeue(
        const std::shared_ptr<Request>& r,
        ServeResult result
    ) {
        if (result == ServeResult::BLOCKED) {
            requeue(r);
            std::this_thread::yield();
            return;
        }

        if (result == ServeResult::FAILED) {
            if (r->op == "PUT" && !r->temp_path.empty()) {
    std::error_code ec;
    fs::remove(r->temp_path, ec);
}
    std::cerr
        << "Request " << r->id
        << " FAILED:"
        << " op=" << r->op
        << " file=" << r->filename
        << " offset=" << r->offset
        << "/" << r->bytes
        << std::endl;

    if (r->op == "PUT" && !r->temp_path.empty()) {
        std::error_code ec;
        fs::remove(r->temp_path, ec);
    }

    send_all(r->fd, "ERR transfer failed\n");

    ::shutdown(r->fd, SHUT_RDWR);
    ::close(r->fd);

    r->finish = mono_ns();
    return;
}

        if (result == ServeResult::DONE) {
            ++r->rounds;

            if (r->op == "PUT") {
                if (!send_all(r->fd, "OK 0\n")) {
                    std::cerr
                        << "PUT final acknowledgement failed:"
                        << " request=" << r->id
                        << std::endl;
                }

            }

            r->finish = mono_ns();

            ::shutdown(r->fd, SHUT_RDWR);
            ::close(r->fd);

            {
                std::lock_guard<std::mutex> lk(all_mtx);
                completed.push_back(r);
            }

            return;
        }

        ++r->rounds;
        requeue(r);
    }

    void worker() {
        while (true) {
            std::shared_ptr<Request> r;

            {
                std::unique_lock<std::mutex> lk(q_mtx);

                q_cv.wait(
                    lk,
                    [&] {
                        return stopping.load() || !queue.empty();
                    }
                );

                if (queue.empty() && stopping.load()) {
                    return;
                }

                if (sched == "sjf") {
                    auto it = std::min_element(
                        queue.begin(),
                        queue.end(),
                        [](const auto& a, const auto& b) {
                            if (a->bytes != b->bytes) {
                                return a->bytes < b->bytes;
                            }

                            return a->arrival < b->arrival;
                        }
                    );

                    r = *it;
                    queue.erase(it);
                } else {
                    r = queue.front();
                    queue.pop_front();
                }

                queue_depth.store(queue.size());
            }

            if (r->rounds == 0 && r->start == 0) {
                r->start = mono_ns();
            }

            ServeResult result = serve_request(r);

            complete_or_requeue(r, result);
        }
    }

    void handle_connection(int fd) {
        std::string line;

        if (!recv_line(fd, line, 3000)) {
            send_all(fd, "ERR invalid request\n");
            ::close(fd);
            return;
        }
        clear_recv_timeout(fd);

        line = trim(line);
        if (line == "HEALTH") {
            uint64_t depth = queue_depth.load();

            send_all(
                fd,
                "OK " + std::to_string(depth) + "\n"
            );

            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);

            return;
        }

        std::istringstream iss(line);
        std::string op;
        std::string name;
        std::string size_text;

        iss >> op >> name >> size_text;

        std::string extra;
        iss >> extra;

        bool malformed =
            (op != "GET" && op != "PUT") ||
            name.empty() ||
            (op == "GET" && !size_text.empty()) ||
            (op == "PUT" &&
             (size_text.empty() || !extra.empty()));
        if (malformed) {
            send_all(fd, "ERR malformed request\n");
            ::close(fd);
            return;
        }

        auto r = std::make_shared<Request>();

        r->id = next_id.fetch_add(1);
        r->fd = fd;
        r->op = op;
        r->filename = name;
        if (!valid_name(name)) {
            send_all(fd, "ERR invalid filename\n");
            ::close(fd);
            return;
        }
        if (op == "GET") {
            fs::path p = root / name;

            if (!fs::exists(p) ||
                !fs::is_regular_file(p)) {
                send_all(fd, "ERR file not found\n");
                ::close(fd);
                return;
            }

            r->bytes = file_size_bytes(p);

            if (!read_get_file(*r)) {
                send_all(fd, "ERR cannot read file\n");
                ::close(fd);
                return;
            }
        }
        else {
    try {
        if (size_text.empty()) {
            throw std::invalid_argument("bad");
        }

        for (char c : size_text) {
            if (c < '0' || c > '9') {
                throw std::invalid_argument("bad");
            }
        }

        size_t pos = 0;

        unsigned long long v =
            std::stoull(size_text, &pos);

        if (pos != size_text.size()) {
            throw std::invalid_argument("bad");
        }

        r->bytes = static_cast<uint64_t>(v);
    }
    catch (...) {
        send_all(fd, "ERR invalid byte count\n");
        ::close(fd);
        return;
    }
}

        r->arrival = mono_ns();

        if (r->op == "PUT") {
            if (!send_all(fd, "OK 0\n")) {
                std::cerr << "PUT admission acknowledgement failed:" << " request=" << r->id << std::endl;
                ::close(fd);
                return;
            }

            r->response_sent = true;
        }

        {
            std::lock_guard<std::mutex> lk(q_mtx);

            queue.push_back(r);
            queue_depth.store(queue.size());
        }

        q_cv.notify_one();
    }

    void write_metrics() {
        std::ofstream out(metrics_path);

        out
            << "request_id,op,filename,bytes,rounds,"
            << "forfeited_bytes,arrival_ns,start_ns,finish_ns\n";

        std::lock_guard<std::mutex> lk(all_mtx);

        std::sort(
            completed.begin(),
            completed.end(),
            [](const auto& a, const auto& b) {
                return a->id < b->id;
            }
        );

        for (const auto& r : completed) {
            out
                << r->id << ','
                << r->op << ','
                << r->filename << ','
                << r->bytes << ','
                << r->rounds << ','
                << r->forfeited << ','
                << r->arrival << ','
                << r->start << ','
                << r->finish
                << '\n';
        }
    }

public:
    SchedulerServer(
        Config c,
        std::string s,
        uint64_t q,
        fs::path f,
        int p,
        std::string m
    )
        : cfg(std::move(c)),
          sched(std::move(s)),
          quantum(q),
          root(std::move(f)),
          packet_lines(p),
          metrics_path(std::move(m)) {}

    int run() {
        if (!fs::exists(root) ||
            !fs::is_directory(root)) {

            throw std::runtime_error(
                "error: --file is not a directory"
            );
        }

        listen_fd = ::socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

        if (listen_fd < 0) {
            throw std::runtime_error(
                "error: socket failed"
            );
        }

        int flags =
            ::fcntl(listen_fd, F_GETFL, 0);

        ::fcntl(
            listen_fd,
            F_SETFL,
            flags | O_NONBLOCK
        );

        int one = 1;

        ::setsockopt(
            listen_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &one,
            sizeof(one)
        );

        sockaddr_in addr{};

        addr.sin_family = AF_INET;

        addr.sin_port =
            htons(
                static_cast<uint16_t>(
                    cfg.port
                )
            );

        if (::inet_pton(
                AF_INET,
                cfg.ip.c_str(),
                &addr.sin_addr) != 1) {

            throw std::runtime_error(
                "error: invalid server.ip"
            );
        }

        if (::bind(
                listen_fd,
                reinterpret_cast<sockaddr*>(&addr),
                sizeof(addr)) < 0) {

            throw std::runtime_error(
                std::string("error: bind failed: ") +
                std::strerror(errno)
            );
        }

        if (::listen(listen_fd, 128) < 0) {
            throw std::runtime_error(
                "error: listen failed"
            );
        }

        instance = this;

        ::signal(SIGINT, signal_handler);
        ::signal(SIGTERM, signal_handler);

        for (int i = 0;
             i < cfg.server_threads;
             ++i) {

            workers.emplace_back(
                &SchedulerServer::worker,
                this
            );
        }

        std::cout
            << "server listening on "
            << cfg.ip
            << ':'
            << cfg.port
            << " scheduler="
            << sched
            << " threads="
            << cfg.server_threads
            << '\n';

        while (!stopping.load()) {
            int fd =
                ::accept(
                    listen_fd,
                    nullptr,
                    nullptr
                );

            if (fd < 0) {
                if (stopping.load()) {
                    break;
                }

                if (errno == EINTR ||
                    errno == EAGAIN ||
                    errno == EWOULDBLOCK) {

                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(20)
                    );

                    continue;
                }

                continue;
            }
            int client_flags = ::fcntl(fd, F_GETFL, 0);

            if (client_flags >= 0) {
                ::fcntl(
                    fd,
                    F_SETFL,
                    client_flags & ~O_NONBLOCK
                );
            }

            handlers.emplace_back(
                &SchedulerServer::handle_connection,
                this,
                fd
            );
        }

        if (listen_fd >= 0) {
            ::shutdown(
                listen_fd,
                SHUT_RDWR
            );

            ::close(listen_fd);

            listen_fd = -1;
        }

        q_cv.notify_all();

        for (auto& t : handlers) {
            if (t.joinable()) {
                t.join();
            }
        }

        q_cv.notify_all();

        for (auto& t : workers) {
            if (t.joinable()) {
                t.join();
            }
        }

        write_metrics();

        {
            std::lock_guard<std::mutex> lk(all_mtx);

            uint64_t total_waiting = 0;
            uint64_t total_response = 0;
            uint64_t min_arrival = 0;
            uint64_t max_finish = 0;

            for (const auto& r : completed) {
                total_waiting += r->start - r->arrival;
                total_response += r->finish - r->arrival;

                if (min_arrival == 0 || r->arrival < min_arrival)
                    min_arrival = r->arrival;

                if (r->finish > max_finish)
                    max_finish = r->finish;
            }

            size_t n = completed.size();

            std::cout << "=== Aggregate Summary ===\n";
            std::cout << "Completed requests: " << n << '\n';

            if (n > 0) {
                std::cout << "Average waiting (ns): " << static_cast<double>(total_waiting) / n << '\n';

                std::cout << "Average response (ns): " << static_cast<double>(total_response) / n << '\n';

                if (max_finish > min_arrival) {
                    double throughput =
                        static_cast<double>(n) / ((max_finish - min_arrival) / 1e9);

                    std::cout << "Throughput (req/s): " << throughput
                        << '\n';
                }
            }
        }

        std::cout << "A14 firings: " << a14_count.load() << '\n';
        return 0;
    }
};

SchedulerServer* SchedulerServer::instance = nullptr;

static void usage() {
    std::cerr
        << "usage: ./server "
        << "--sched <fcfs|sjf|rr|drr> "
        << "[--quantum Q] "
        << "--file <path> "
        << "[--p N] "
        << "[--config path] "
        << "[--metrics-out path]\n";
}

int main(int argc, char** argv) {
    std::string sched;
    std::string file;
    std::string config = "config.json";
    std::string metrics = "metrics.csv";

    uint64_t quantum = 0;

    int p = 1;

    bool quantum_seen = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        auto need =
            [&](const char* n) -> std::string {

                if (i + 1 >= argc) {
                    std::cerr
                        << "error: missing value for "
                        << n
                        << '\n';

                    std::exit(2);
                }

                return argv[++i];
            };

        if (a == "--sched") {
            sched = need("--sched");
        }

        else if (a == "--quantum") {
            quantum_seen = true;

            try {
                quantum =
                    std::stoull(
                        need("--quantum")
                    );
            }
            catch (...) {
                std::cerr
                    << "error: invalid --quantum\n";

                return 2;
            }
        }

        else if (a == "--file") {
            file = need("--file");
        }

        else if (a == "--p") {
            try {
                p =
                    std::stoi(
                        need("--p")
                    );
            }
            catch (...) {
                std::cerr
                    << "error: invalid --p\n";

                return 2;
            }
        }

        else if (a == "--config") {
            config = need("--config");
        }

        else if (a == "--metrics-out") {
            metrics = need("--metrics-out");
        }

        else {
            usage();
            return 2;
        }
    }

    if (sched.empty() || file.empty()) {
        usage();
        return 2;
    }

    if (sched != "fcfs" &&
        sched != "sjf" &&
        sched != "rr" &&
        sched != "drr") {

        std::cerr
            << "error: invalid --sched\n";

        return 2;
    }

    if ((sched == "rr" ||
         sched == "drr") &&
        (!quantum_seen ||
         quantum == 0)) {

        std::cerr
            << "error: --quantum is required for rr or drr\n";

        return 2;
    }

    if ((sched == "fcfs" ||
         sched == "sjf") &&
        quantum_seen) {

        std::cerr
            << "error: --quantum is rejected for fcfs and sjf\n";

        return 2;
    }

    if (p < 1) {
        std::cerr
            << "error: --p must be positive\n";

        return 2;
    }

    try {
        Config cfg =
            load_config(config);

        SchedulerServer server(
            cfg,
            sched,
            quantum,
            file,
            p,
            metrics
        );

        return server.run();
    }
    catch (const std::exception& e) {
        std::cerr
            << e.what()
            << '\n';

        return 1;
    }
}