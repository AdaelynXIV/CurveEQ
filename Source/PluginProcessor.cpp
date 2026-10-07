#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static constexpr int kChunk = 32;
static constexpr int kTaps[4] = { 2048, 4096, 8192, 16384 };
static String pid (int b, const char* n) { return "b" + String (b + 1) + "_" + n; }

AudioProcessorValueTreeState::ParameterLayout EqProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    NormalisableRange<float> fr (10.f, 30000.f, 0.f);  fr.setSkewForCentre (1000.f);
    NormalisableRange<float> qr (0.1f, 40.f, 0.f);     qr.setSkewForCentre (1.f);
    NormalisableRange<float> gr (-30.f, 30.f, 0.01f);
    NormalisableRange<float> dr (-30.f, 30.f, 0.01f);
    NormalisableRange<float> tr (-80.f, 0.f, 0.1f);
    NormalisableRange<float> ar (0.1f, 500.f, 0.f);    ar.setSkewForCentre (20.f);
    NormalisableRange<float> rr (5.f, 2000.f, 0.f);    rr.setSkewForCentre (150.f);

    auto attr = [] (const String& suffix, int dec)
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([=] (float v, int) { return String (v, dec) + suffix; })
            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });
    };
    auto freqAttr = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return v >= 1000.f ? String (v / 1000.f, 2) + " kHz" : String (v, v < 100.f ? 1 : 0) + " Hz"; })
        .withValueFromStringFunction ([] (const String& t) { auto v = t.getFloatValue(); return t.containsIgnoreCase ("k") ? v * 1000.f : v; });

    for (int i = 0; i < eq::kNumBands; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        const float defF = 40.f * std::pow (400.f, (float) i / (eq::kNumBands - 1));
        auto id = [&] (const char* s) { return ParameterID { pid (i, s), 1 }; };
        p.push_back (std::make_unique<AudioParameterBool> (id ("on"), n + "On", false));
        p.push_back (std::make_unique<AudioParameterChoice> (id ("type"), n + "Shape",
            StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" }, 0));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("freq"), n + "Frequency", fr, defF, freqAttr));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("gain"), n + "Gain", gr, 0.f, attr (" dB", 1)));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("q"), n + "Q", qr, 1.f, attr ("", 2)));
        p.push_back (std::make_unique<AudioParameterChoice> (id ("slope"), n + "Slope",
            StringArray { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" }, 1));
        p.push_back (std::make_unique<AudioParameterChoice> (id ("mode"), n + "Channels",
            StringArray { "Stereo", "Left", "Right", "Mid", "Side" }, 0));
        p.push_back (std::make_unique<AudioParameterBool> (id ("dyn"), n + "Dynamic", false));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("dr"), n + "Dynamic Range", dr, -6.f, attr (" dB", 1)));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("dthr"), n + "Dynamic Threshold", tr, -30.f, attr (" dB", 1)));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("datt"), n + "Dynamic Attack", ar, 10.f, attr (" ms", 1)));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("drel"), n + "Dynamic Release", rr, 120.f, attr (" ms", 0)));
        p.push_back (std::make_unique<AudioParameterChoice> (id ("ddir"), n + "Dynamic Trigger",
            StringArray { "Above", "Below" }, 0));
    }
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "out_gain", 1 }, "Output Gain",
                                                        NormalisableRange<float> (-24.f, 24.f, 0.01f), 0.f, attr (" dB", 1)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "phase", 1 }, "Processing Mode",
                                                         StringArray { "Zero Latency", "Linear Phase" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "lquality", 1 }, "Linear Phase Quality",
                                                         StringArray { "Low", "Medium", "High", "Max" }, 2));
    return { p.begin(), p.end() };
}

// Background thread that designs linear-phase impulse responses and hands them to the convolvers.
struct EqProcessor::IRThread : Thread
{
    explicit IRThread (EqProcessor& o) : Thread ("Curve EQ IR builder"), owner (o) {}
    void run() override
    {
        std::vector<float> last;
        while (! threadShouldExit())
        {
            owner.rebuildIR (last);
            wait (30);
        }
    }
    EqProcessor& owner;
};

EqProcessor::EqProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int i = 0; i < eq::kNumBands; ++i)
    {
        auto g = [&] (const char* s) { return apvts.getRawParameterValue (pid (i, s)); };
        bp[(size_t) i] = { g ("on"), g ("type"), g ("freq"), g ("gain"), g ("q"), g ("slope"), g ("mode"),
                           g ("dyn"), g ("dr"), g ("dthr"), g ("datt"), g ("drel"), g ("ddir") };
        dynAmt[(size_t) i].store (0.f);
    }
    outGain  = apvts.getRawParameterValue ("out_gain");
    phaseP   = apvts.getRawParameterValue ("phase");
    qualityP = apvts.getRawParameterValue ("lquality");
    apvts.addParameterListener ("phase", this);
    apvts.addParameterListener ("lquality", this);
    irThread = std::make_unique<IRThread> (*this);
}

EqProcessor::~EqProcessor()
{
    apvts.removeParameterListener ("phase", this);
    apvts.removeParameterListener ("lquality", this);
    irThread->stopThread (3000);
    cancelPendingUpdate();
}

int EqProcessor::latencyForSettings() const
{
    return isLinearPhase() ? kTaps[jlimit (0, 3, (int) qualityP->load())] / 2 : 0;
}

void EqProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    irThread->stopThread (3000);
    fs = sampleRate;
    maxBlock = jmax (32, samplesPerBlock);
    mono.store (getTotalNumInputChannels() < 2);
    for (auto& s : state) s = {};

    dsp::ProcessSpec spec { sampleRate, (uint32) maxBlock, 1 };
    for (auto& c : conv) { c.prepare (spec); c.reset(); }
    tmp.setSize (4, maxBlock);
    crossActive.store (false);
    setLatencySamples (latencyForSettings());
    irThread->startThread();
}

bool EqProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == AudioChannelSet::mono() || o == AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
}

EqProcessor::Snap EqProcessor::snapshot (int b) const
{
    const auto& p = bp[(size_t) b];
    Snap s;
    s.type = (int) p.type->load();   s.mode = (int) p.mode->load();   s.slope = (int) p.slope->load();
    s.dir = (int) p.dir->load();     s.freq = p.freq->load();         s.gain = p.gain->load();
    s.q = p.q->load();               s.range = p.range->load();       s.thr = p.thr->load();
    s.att = p.att->load();           s.rel = p.rel->load();
    s.dyn = p.dyn->load() > 0.5f && (s.type == eq::Bell || s.type == eq::LowShelf || s.type == eq::HighShelf);
    return s;
}

// Runs the band's level detector over m samples and returns how much of the dynamic range to apply (0..1).
float EqProcessor::detect (BandState& st, const Snap& s, int nCh, const float* l, const float* r, int m, double freq)
{
    const int dt = s.type == eq::LowShelf ? (int) eq::HighCut : s.type == eq::HighShelf ? (int) eq::LowCut : (int) eq::BandPass;
    const double dq = s.type == eq::Bell ? std::max ((double) s.q, 0.5) : 0.7071;
    const auto c = eq::biquad (dt, fs, freq, 0.0, dq);
    const double ca = 1.0 - std::exp (-1.0 / (fs * std::max (s.att, 0.05f) * 0.001));
    const double cr = 1.0 - std::exp (-1.0 / (fs * std::max (s.rel, 1.0f) * 0.001));
    double env = st.env, z1 = st.dz[0], z2 = st.dz[1];

    for (int i = 0; i < m; ++i)
    {
        float x = l[i];
        if (nCh > 1)
        {
            switch (s.mode)
            {
                case eq::Left:  x = l[i]; break;
                case eq::Right: x = r[i]; break;
                case eq::Side:  x = 0.5f * (l[i] - r[i]); break;
                default:        x = 0.5f * (l[i] + r[i]); break;
            }
        }
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        const double a = std::abs (y);
        env += (a > env ? ca : cr) * (a - env);
    }
    st.env = env; st.dz[0] = z1; st.dz[1] = z2;

    const double level = 20.0 * std::log10 (env + 1e-9);
    float amt = (float) jlimit (0.0, 1.0, (level - s.thr) / 10.0 + 0.5); // 10 dB soft knee around the threshold
    return s.dir == 1 ? 1.f - amt : amt;
}

static void runSection (const eq::Coeffs& c, double* z, float* d, int n)
{
    double z1 = z[0], z2 = z[1];
    for (int i = 0; i < n; ++i)
    {
        const double x = d[i];
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        d[i] = (float) y;
    }
    z[0] = z1; z[1] = z2;
}

void EqProcessor::processMinPhase (float* L, float* R, int n)
{
    const int nCh = R ? 2 : 1;
    const double smooth = 1.0 - std::exp (-(double) kChunk / (fs * 0.02)); // ~20 ms parameter smoothing

    for (int b = 0; b < eq::kNumBands; ++b)
    {
        auto& st = state[(size_t) b];
        if (bp[(size_t) b].on->load() < 0.5f) { st.wasOn = false; dynAmt[(size_t) b].store (0.f); continue; }

        const Snap s = snapshot (b);
        if (! st.wasOn)
        {
            std::memset (st.z, 0, sizeof (st.z)); st.dz[0] = st.dz[1] = 0; st.env = 0;
            st.cf = std::log ((double) s.freq); st.cg = s.gain; st.cq = std::log ((double) s.q);
            st.wasOn = true;
        }
        const double tlf = std::log ((double) s.freq), tlq = std::log ((double) s.q);
        float lastAmt = 0.f;

        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int m = jmin (kChunk, n - pos);
            float* l = L + pos;
            float* r = R ? R + pos : nullptr;

            st.cf += smooth * (tlf - st.cf);
            st.cg += smooth * ((double) s.gain - st.cg);
            st.cq += smooth * (tlq - st.cq);
            const double f = std::exp (st.cf);
            double g = st.cg;
            if (s.dyn) { lastAmt = detect (st, s, nCh, l, r, m, f); g += (double) s.range * lastAmt; }

            const auto sec = eq::design (s.type, fs, f, g, std::exp (st.cq), s.slope);
            auto run = [&] (float* d, int ch) { for (int k = 0; k < sec.n; ++k) runSection (sec.c[k], st.z[ch][k], d, m); };

            if (r == nullptr || s.mode == eq::Stereo) { run (l, 0); if (r) run (r, 1); }
            else if (s.mode == eq::Left)  run (l, 0);
            else if (s.mode == eq::Right) run (r, 1);
            else
            {
                for (int i = 0; i < m; ++i) { const float mid = (l[i] + r[i]) * 0.5f, side = (l[i] - r[i]) * 0.5f; l[i] = mid; r[i] = side; }
                if (s.mode == eq::Mid) run (l, 0); else run (r, 1);
                for (int i = 0; i < m; ++i) { const float mid = l[i], side = r[i]; l[i] = mid + side; r[i] = mid - side; }
            }
        }
        dynAmt[(size_t) b].store (s.dyn ? lastAmt : 0.f);
    }
}

void EqProcessor::processLinear (float* L, float* R, int n)
{
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        auto& st = state[(size_t) b];
        st.wasOn = false;
        if (bp[(size_t) b].on->load() < 0.5f) { dynAmt[(size_t) b].store (0.f); continue; }
        const Snap s = snapshot (b);
        dynAmt[(size_t) b].store (s.dyn ? detect (st, s, R ? 2 : 1, L, R, n, s.freq) : 0.f);
    }
    runConvolution (L, R, n);
}

void EqProcessor::runConvolution (float* L, float* R, int n)
{
    const bool cross = R != nullptr && crossActive.load();
    auto run = [] (dsp::Convolution& c, float* d, int m)
    {
        float* ch[1] = { d };
        dsp::AudioBlock<float> blk (ch, 1, (size_t) m);
        dsp::ProcessContextReplacing<float> ctx (blk);
        c.process (ctx);
    };

    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int m = jmin (maxBlock, n - pos);
        if (! cross)
        {
            run (conv[0], L + pos, m);
            if (R) run (conv[3], R + pos, m);
            continue;
        }
        for (int c = 0; c < 4; ++c)
            FloatVectorOperations::copy (tmp.getWritePointer (c), (c & 1) ? R + pos : L + pos, m);
        for (int c = 0; c < 4; ++c) run (conv[c], tmp.getWritePointer (c), m);
        for (int i = 0; i < m; ++i)
        {
            L[pos + i] = tmp.getReadPointer (0)[i] + tmp.getReadPointer (1)[i];
            R[pos + i] = tmp.getReadPointer (2)[i] + tmp.getReadPointer (3)[i];
        }
    }
}

void EqProcessor::rebuildIR (std::vector<float>& last)
{
    if (! isLinearPhase()) { last.clear(); return; }

    const int N = kTaps[jlimit (0, 3, (int) qualityP->load())];
    const bool isMono = mono.load();
    std::vector<float> sig { (float) fs, (float) N, isMono ? 1.f : 0.f };
    std::vector<eq::BandDesc> bands;

    for (int b = 0; b < eq::kNumBands; ++b)
    {
        if (bp[(size_t) b].on->load() < 0.5f) continue;
        const Snap s = snapshot (b);
        double g = s.gain;
        if (s.dyn) g += (double) s.range * dynAmt[(size_t) b].load();
        g = std::round (g * 20.0) / 20.0;
        bands.push_back ({ s.type, s.mode, s.slope, (double) s.freq, g, (double) s.q });
        for (float v : { (float) b, (float) s.type, (float) s.mode, (float) s.slope, s.freq, (float) g, s.q }) sig.push_back (v);
    }
    if (sig == last) return;
    last = sig;

    std::array<std::vector<float>, 4> ir;
    bool cross = false;
    eq::buildIRs (bands, fs, N, isMono, ir, cross);

    auto load = [&] (int i)
    {
        AudioBuffer<float> buf (1, (int) ir[(size_t) i].size());
        buf.copyFrom (0, 0, ir[(size_t) i].data(), buf.getNumSamples());
        conv[i].loadImpulseResponse (std::move (buf), fs, dsp::Convolution::Stereo::no,
                                     dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
    };
    load (0); load (3);
    if (cross) { load (1); load (2); }
    crossActive.store (cross);
}

void EqProcessor::processBlock (AudioBuffer<float>& buf, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int nCh = jmin (2, buf.getNumChannels()), n = buf.getNumSamples();
    if (nCh == 0 || n == 0) return;
    float* L = buf.getWritePointer (0);
    float* R = nCh > 1 ? buf.getWritePointer (1) : nullptr;
    mono.store (R == nullptr, std::memory_order_relaxed);

    if (isLinearPhase()) processLinear (L, R, n);
    else                 processMinPhase (L, R, n);

    buf.applyGain (Decibels::decibelsToGain (outGain->load()));

    const int w = wpos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        ring[(size_t) ((w + i) & (kRing - 1))] = R ? 0.5f * (L[i] + R[i]) : L[i];
    wpos.store ((w + n) & (kRing - 1), std::memory_order_release);
}

void EqProcessor::copyLatest (float* dest, int num) const
{
    const int w = wpos.load (std::memory_order_acquire);
    for (int i = 0; i < num; ++i)
        dest[i] = ring[(size_t) ((w - num + i) & (kRing - 1))];
}

AudioProcessorEditor* EqProcessor::createEditor() { return new EqEditor (*this); }

void EqProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void EqProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EqProcessor(); }
