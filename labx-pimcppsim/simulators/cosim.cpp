// Trace-driven CPU, DMA engine, and single CIM macro.
#include "timing.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Tick = Cycles;
static constexpr TimingModel timing{};
using Args = std::vector<std::string>;
using Bytes = std::vector<std::uint8_t>;
static void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
static long long number(const std::string& s) {
    long long v;
    auto [p,e] = std::from_chars(s.data(), s.data()+s.size(), v);
    require(e == std::errc{} && p == s.data()+s.size(), "invalid decimal: " + s);
    return v;
}
static Tick natural(const std::string& s) {
    auto n = number(s); require(n >= 0, "expected nonnegative integer"); return Tick(n);
}
static Tick after(Tick t, Tick d) {
    require(d <= std::numeric_limits<Tick>::max()-t, "cycle counter overflow"); return t+d;
}
struct Range {
    Tick addr, size;
    bool intersects(Range b) const { return addr < b.addr+b.size && b.addr < addr+size; }
};
class SharedMemory {
    Bytes data = Bytes(1<<20);
public:
    Range region(Tick a, Tick n) const {
        require(a <= data.size() && n && n <= data.size()-a, "invalid memory range");
        return {a,n};
    }
    Bytes read(Range r) const { return {data.begin()+r.addr,data.begin()+r.addr+r.size}; }
    void write(Tick a, const Bytes& b) { std::copy(b.begin(), b.end(), data.begin()+a); }
};
static int signedByte(unsigned b) { return b < 128 ? int(b) : int(b)-256; }
class CimMacro {
    Bytes weights;
public:
    Tick rows=0, cols=0;
    void program(Tick r, Tick c, Bytes w) { rows=r; cols=c; weights=std::move(w); }
    Bytes multiply(const Bytes& x) const {
        Bytes y;
        for (Tick c=0; c<cols; ++c) {
            std::int64_t sum=0;
            for (Tick r=0;r<rows;++r) sum += signedByte(x[r])*signedByte(weights[r*cols+c]);
            auto bits=std::uint64_t(sum);
            for (int k=0;k<8;++k) { y.push_back(bits & 255); bits >>= 8; }
        }
        return y;
    }
};

// With one job in flight the only future port requests are its input and
// output bursts. A CPU burst granted during compute may delay writeback.
struct DmaJob {
    std::string id;
    Range input, output;
    Bytes result;
    Tick inputEnd, computeEnd, writeStart, writeEnd;
};
class DmaEngine {
    std::optional<DmaJob> job;
public:
    Tick done(Tick now) const { return job ? job->writeEnd : now; }
    void launch(std::string id, Range x, Range y, Tick t, const SharedMemory& mem, const CimMacro& cim) {
        Tick captured = after(t, timing.input(cim.rows));
        Tick computed = after(captured, timing.compute(cim.rows, cim.cols));
        // No writer can change the input during capture. Evaluate eagerly but
        // keep the result private until the scheduled DMA commit event.
        Tick visible = after(computed, timing.readout(cim.cols));
        job = DmaJob{id, x, y, cim.multiply(mem.read(x)), captured, computed, computed, visible};
    }
    Tick grantCpu(Tick t, Range r, bool store) {
        if (!job) return t;
        auto& j=*job;
        if (r.intersects(j.output)) return j.writeEnd;
        if (store && r.intersects(j.input)) t=std::max(t,j.inputEnd);
        t=std::max(t,j.inputEnd); // port occupied even for unrelated addresses
        if (t>=j.computeEnd) return std::max(t,j.writeEnd); // DMA wins ties
        auto end = after(t, timing.beats(r.size));
        j.writeStart=std::max(j.writeStart,end);
        j.writeEnd = after(j.writeStart, timing.beats(j.output.size));
        return t;
    }
    void advance(Tick t, SharedMemory& mem) {
        if (!job || t<job->writeEnd) return;
        mem.write(job->output.addr,job->result);
        std::cout << "job="<<job->id<<" input_done="<<job->inputEnd<<" compute_done="<<job->computeEnd
                  <<" write_start="<<job->writeStart<<" visible="<<job->writeEnd<<'\n';
        job.reset();
    }
    bool running(const std::string& id) const { return job && job->id==id; }
};
class SystemSimulator {
    SharedMemory mem;
    CimMacro cim;
    DmaEngine dma;
    Tick now=0, stalls=0;
    bool overlap;
    std::set<std::string> issued;
    void advance(Tick t) { dma.advance(t,mem); now=t; }
    void block(Tick t) { stalls+=t-now; advance(t); }
    void access(Range r,bool write) {
        block(dma.grantCpu(now, r, write));
        advance(after(now, timing.beats(r.size)));
    }
public:
    explicit SystemSimulator(bool overlap):overlap(overlap) {}
    void execute(const Args& a) {
        const auto& op=a[0];
        auto arity=[&](unsigned n){require(a.size()==n+1,"wrong operand count for "+op);};
        if (op=="STORE") {
            require(a.size()>=3,"STORE address byte...");
            auto r=mem.region(natural(a[1]),a.size()-2); Bytes b;
            for (size_t i=2;i<a.size();++i) {
                auto v=number(a[i]); require(v>=-128 && v<=255,"byte out of range"); b.push_back(std::uint8_t(v));
            }
            access(r,true); mem.write(r.addr,b);
        } else if (op=="LOAD") {
            arity(2); auto n=natural(a[2]); require(n<=131072,"too many int64 values");
            auto r=mem.region(natural(a[1]),8*n); access(r,false); auto b=mem.read(r);
            std::cout<<"load@"<<r.addr<<" cycle="<<now<<" values:";
            for (size_t i=0;i<b.size();i+=8) {
                Tick u=0; for (int k=7;k>=0;--k) u=(u<<8)|b[i+k];
                auto v=u>>63 ? -1-std::int64_t(~u) : std::int64_t(u); std::cout<<' '<<v;
            }
            std::cout<<'\n';
        } else if (op=="PROGRAM") {
            arity(3); auto r=natural(a[2]),c=natural(a[3]);
            require(r && r <= timing.tileRows && c && c <= timing.tileCols,
                    "matrix must fit " + std::to_string(timing.tileRows) + "x" +
                    std::to_string(timing.tileCols) + " macro");
            auto w=mem.region(natural(a[1]),r*c); block(dma.done(now));
            auto cost = timing.program(r, c);
            block(after(now, cost));
            cim.program(r, c, mem.read(w));
            std::cout<<"program_cycles="<<cost<<" ready="<<now<<'\n';
        } else if (op=="SUBMIT") {
            arity(3); require(cim.rows,"PROGRAM required"); require(issued.insert(a[1]).second,"duplicate job id");
            auto x=mem.region(natural(a[2]),cim.rows), y=mem.region(natural(a[3]),8*cim.cols);
            block(dma.done(now)); advance(after(now,2)); dma.launch(a[1],x,y,now,mem,cim);
            if (!overlap) block(dma.done(now));
        } else if (op=="WAIT") {
            arity(1); require(issued.count(a[1]),"unknown job id"); if (dma.running(a[1])) block(dma.done(now));
        } else if (op=="WORK") {
            arity(1); advance(after(now,natural(a[1])));
        } else throw std::runtime_error("unknown opcode: "+op);
    }
    void finish() {
        Tick retired=now; advance(dma.done(now));
        std::cout<<"cpu_finished="<<retired<<" cpu_blocked="<<stalls<<" total_cycles="<<now<<'\n';
    }
};
class TraceCpu {
public:
    void run(std::istream& in,SystemSimulator& system) const {
        std::string line; unsigned lineNo=0;
        while (std::getline(in,line)) {
            ++lineNo; std::istringstream words(line.substr(0,line.find('#'))); Args a;
            for (std::string s;words>>s;) a.push_back(s);
            if (a.empty()) continue;
            try { system.execute(a); }
            catch(const std::exception& e) { throw std::runtime_error("line "+std::to_string(lineNo)+": "+e.what()); }
        }
        require(!in.bad(),"trace read failed"); system.finish();
    }
};
int main(int argc,char** argv) {
    if (argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--blocking")) {
        std::cerr<<"Usage: cosim TRACE|- [--blocking]\n"; return 1;
    }
    try {
        SystemSimulator system(argc==2); TraceCpu cpu;
        if (std::string(argv[1])=="-") cpu.run(std::cin,system);
        else { std::ifstream in(argv[1]); require(bool(in),"cannot open trace"); cpu.run(in,system); }
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
