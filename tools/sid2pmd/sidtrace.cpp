// SPDX-License-Identifier: GPL-2.0-or-later
// Cycle-stamped SID register capture using the unmodified libsidplayfp core.
#include "sidplayfp/sidplayfp.h"
#include "sidplayfp/SidTune.h"
#include "sidplayfp/SidTuneInfo.h"
#include "sidplayfp/SidConfig.h"
#include "sidplayfp/sidbuilder.h"
#include "sidemu.h"
#include "builders/sidlite-builder/sidlite/SID.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace libsidplayfp;
struct Capture {
    std::ofstream out;
    uint64_t origin = 0;
    bool started = false;
    std::array<uint8_t, 32> regs{};
    explicit Capture(const char* path): out(path, std::ios::binary) {
        if (!out) throw std::runtime_error("Cannot create trace file");
    }
    void number(uint64_t n, int bytes) {
        for (int i=0; i<bytes; ++i) out.put(static_cast<char>(n >> (i*8)));
    }
    void record(uint64_t time, uint8_t reg, uint8_t value) {
        regs[reg] = value;
        if (started && time >= origin) {
            number(time-origin, 8); out.put(reg); out.put(value);
        }
    }
};
class TraceSid final: public sidemu {
    SIDLite::SID sid;
    Capture& capture;
    std::array<short, 2048> samples{};
public:
    TraceSid(sidbuilder* builder, Capture& cap): sidemu(builder), capture(cap) { m_buffer=samples.data(); }
    void reset(uint8_t volume) override {
        m_accessClk=0; sid.reset(); sid.write(0x18,volume);
        capture.regs.fill(0); capture.regs[0x18]=volume;
    }
    void clock() override {
        const auto now=eventScheduler->getTime(EVENT_CLOCK_PHI1);
        m_bufferpos += sid.clock(now-m_accessClk, m_buffer+m_bufferpos);
        m_accessClk=now;
    }
    uint8_t read(uint_least8_t reg) override { clock(); return sid.read(reg); }
    void write(uint_least8_t reg, uint8_t value) override {
        clock(); capture.record(eventScheduler->getTime(EVENT_CLOCK_PHI1),reg,value); sid.write(reg,value);
    }
    void sampling(float clock, float freq, SidConfig::sampling_method_t) override {
        m_status=sid.setSamplingParameters(static_cast<unsigned>(clock),static_cast<unsigned short>(freq));
    }
    void model(SidConfig::sid_model_t model, bool) override {
        sid.setChipModel(model==SidConfig::MOS8580 ? SIDLite::SID::model_t::MOS8580 : SIDLite::SID::model_t::MOS6581);
    }
    uint64_t time() const { return eventScheduler->getTime(EVENT_CLOCK_PHI1); }
};
class TraceBuilder final: public sidbuilder {
    Capture& capture;
    sidemu* create() override { device=new TraceSid(this,capture); return device; }
    const char* getCredits() const override { return "sid2pmd register capture / libsidplayfp"; }
public:
    TraceSid* device=nullptr;
    explicit TraceBuilder(Capture& cap): sidbuilder("sid2pmd"), capture(cap) {}
    ~TraceBuilder() override { remove(); }
};
static std::vector<uint8_t> rom(const char* path, size_t size) {
    if (std::string(path)=="-") return {};
    std::ifstream file(path,std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.size()!=size) throw std::runtime_error("Incorrect ROM size or unreadable ROM");
    return bytes;
}
int main(int argc, char** argv) {
    try {
        if(argc!=10) throw std::runtime_error("Usage: sidtrace input.sid output.trace seconds subtune pal|ntsc kernal|- basic|- chargen|- warmup");
        double duration=std::stod(argv[3]), warmup=std::stod(argv[9]);
        unsigned song=std::stoul(argv[4]);
        if(!std::isfinite(duration)||duration<=0||duration>900||!std::isfinite(warmup)||warmup<0||warmup>60)
            throw std::runtime_error("Invalid capture duration");
        SidTune tune(argv[1]);
        if(!tune.getStatus()) throw std::runtime_error(tune.statusString());
        if(song>tune.getInfo()->songs()) throw std::runtime_error("Subtune out of range");
        tune.selectSong(song);
        if(tune.getInfo()->sidChips()!=1) throw std::runtime_error("Only single-SID tunes are supported");
        Capture capture(argv[2]);
        TraceBuilder builder(capture);
        sidplayfp engine;
        auto kernal=rom(argv[6],8192), basic=rom(argv[7],8192), chargen=rom(argv[8],4096);
        if(!kernal.empty()) engine.setRoms(kernal.data(), basic.empty()?nullptr:basic.data(), chargen.empty()?nullptr:chargen.data());
        SidConfig config=engine.config();
        config.sidEmulation=&builder; config.frequency=8000; config.powerOnDelay=0;
        config.defaultC64Model=std::string(argv[5])=="ntsc"?SidConfig::NTSC:SidConfig::PAL;
        config.forceC64Model=true;
        if(!engine.config(config)||!engine.load(&tune)) throw std::runtime_error(engine.error());
        const uint32_t clock=config.defaultC64Model==SidConfig::PAL ? 985248 : 1022727;
        const uint64_t ready=builder.device->time()+static_cast<uint64_t>(warmup*clock);
        // libsidplayfp::play advances scheduler events, whose count differs
        // from elapsed CPU cycles when VIC/CIA events share a clock edge.
        while(builder.device->time()<ready) {
            auto n=static_cast<unsigned>(std::min<uint64_t>(ready-builder.device->time(),5000));
            if(engine.play(n)<0) throw std::runtime_error(engine.error()); }
        capture.origin=builder.device->time();
        capture.out.write("S2PT",4); capture.number(1,4); capture.number(clock,4);
        capture.number(static_cast<uint64_t>(duration*clock),8);
        capture.out.write(reinterpret_cast<const char*>(capture.regs.data()),32);
        capture.started=true;
        const uint64_t finish=capture.origin+static_cast<uint64_t>(duration*clock);
        while(builder.device->time()<finish) {
            auto n=static_cast<unsigned>(std::min<uint64_t>(finish-builder.device->time(),5000));
            if(engine.play(n)<0) throw std::runtime_error(engine.error()); }
        capture.out.flush();
        if(!capture.out) throw std::runtime_error("Failed writing trace");
        std::cout << "Captured " << duration << " seconds, subtune " << tune.getInfo()->currentSong() << '\n';
        return 0;
    } catch(const std::exception& e) { std::cerr << "sidtrace: " << e.what() << '\n'; return 1; }
}
