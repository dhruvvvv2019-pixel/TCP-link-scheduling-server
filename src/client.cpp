#include "common.hpp"
#include "config.hpp"
struct Cfg { std::string ip; int port; int client_threads; 
};
static Cfg read_cfg(const std::string& path) {
    Config c = load_config(path);
    return {c.ip, c.port, c.client_threads};
}

static int connect_to(const Cfg& c) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) return -1;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(c.port); if (::inet_pton(AF_INET,c.ip.c_str(),&a.sin_addr)!=1) { ::close(fd); return -1; }
    if (::connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))<0) { ::close(fd); return -1; } return fd;
}
static bool put_file(const Cfg& c, const fs::path& p) {
    std::ifstream in(p,std::ios::binary); if(!in) return false;
    uint64_t n=fs::file_size(p); int fd=connect_to(c); 
    if(fd<0) return false;
    std::string name=p.filename().string(); if(!send_all(fd,"PUT "+name+" "+std::to_string(n)+"\n")){::close(fd);return false;}
    std::string line; if(!recv_line(fd,line)) {::close(fd);return false;} if(line.rfind("OK ",0)!=0){::close(fd);return false;}
    std::vector<char> buf(8192);
while (in) {
    in.read(buf.data(), buf.size());
    std::streamsize g = in.gcount();
    if (g > 0) {
        if (!send_all(fd,buf.data(),static_cast<size_t>(g))) {
            ::close(fd);
            return false;
        }
    }
}
    bool ok=recv_line(fd,line)&&line.rfind("OK 0",0)==0; ::close(fd); return ok;
}

static bool get_file(const Cfg& c, const std::string& name) {
    int fd=connect_to(c); if(fd<0)return false; if(!send_all(fd,"GET "+name+"\n")){::close(fd);return false;}
    std::string line; if(!recv_line(fd,line)){::close(fd);return false;} std::istringstream iss(line);std::string ok;uint64_t n; if(!(iss>>ok>>n)||ok!="OK"){::close(fd);return false;}
    std::ofstream out(name,std::ios::binary|std::ios::trunc); if(!out){::close(fd);return false;} std::vector<char> buf(8192);uint64_t got=0; while(got<n){size_t want=static_cast<size_t>(std::min<uint64_t>(buf.size(),n-got));ssize_t r=::recv(fd,buf.data(),want,0);if(r>0){out.write(buf.data(),r);got+=r;}else if(r<0&&errno==EINTR)continue;else{::close(fd);return false;}}::close(fd);return got==n;
}
static void usage(){std::cerr<<"usage: ./client put <local-path> | get <name> | load <workload-dir> --requests N [--config path]\n";}

int main(int argc,char**argv){
    if(argc<3){usage();return 2;}std::string op=argv[1], target=argv[2],config="config.json";long long requests=0;
    for(int i=3;i<argc;++i){std::string a=argv[i];if(a=="--config"&&i+1<argc)config=argv[++i];else if(a=="--requests"&&i+1<argc)requests=std::stoll(argv[++i]);else{usage();return 2;}}
    try{Cfg c=read_cfg(config);if(op=="put"){if(requests>0){usage();return 2;}return put_file(c,target)?0:1;}if(op=="get"){if(requests>0){usage();return 2;}return get_file(c,target)?0:1;}if(op!="load"||requests<1){usage();return 2;}
        std::vector<fs::path> files;for(auto&e:fs::directory_iterator(target))if(e.is_regular_file())files.push_back(e.path());
        if(files.empty())throw std::runtime_error("workload directory is empty");
        for(auto&p:files)if(!put_file(c,p))throw std::runtime_error("seeding failed for "+p.string());
        std::atomic<long long> next{0};std::atomic<bool> bad{false};int threads=c.client_threads;std::vector<std::thread> ts;std::mt19937_64 rng(1234567);std::mutex rng_m;
        for(int t=0;t<threads;++t)ts.emplace_back([&]{while(true){long long i=next.fetch_add(1);if(i>=requests)break;size_t k;{std::lock_guard<std::mutex>lk(rng_m);k=static_cast<size_t>(rng()%files.size());}bool isget;{std::lock_guard<std::mutex>lk(rng_m);isget=(rng()%2)==0;}bool ok=isget?get_file(c,files[k].filename().string()):put_file(c,files[k]);if(!ok)bad.store(true);}});
        for(auto&t:ts) t.join();
        return bad.load()?1:0;
    }catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
}
