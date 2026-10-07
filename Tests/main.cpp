#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
using namespace juce;

static void setP (EqProcessor& p, const String& id, float plain)
{
    auto* q = p.apvts.getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (plain));
}
static void band (EqProcessor& p, int b, int type, float f, float g, float q, int mode = 0, int slope = 1)
{
    String s = "b" + String (b) + "_";
    setP (p, s + "on", 1); setP (p, s + "type", (float) type); setP (p, s + "freq", f); setP (p, s + "gain", g);
    setP (p, s + "q", q); setP (p, s + "mode", (float) mode); setP (p, s + "slope", (float) slope);
}
static void reset (EqProcessor& p) { for (int b = 1; b <= 24; ++b) { setP (p, "b" + String (b) + "_on", 0); setP (p, "b" + String (b) + "_dyn", 0); } }

// kind: 0 L=R, 1 L=-R, 2 L only.  returns output L/R
struct Out { std::vector<float> L, R; };
static Out run (EqProcessor& p, double f, float amp, int kind, double secs, double fs, int sleepMs = 0)
{
    const int block = 256, total = (int) (secs * fs);
    AudioBuffer<float> buf (2, block); MidiBuffer mb; Out o;
    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const float x = amp * (float) std::sin (2.0 * MathConstants<double>::pi * f * (pos + i) / fs);
            buf.setSample (0, i, x);
            buf.setSample (1, i, kind == 0 ? x : kind == 1 ? -x : 0.f);
        }
        p.processBlock (buf, mb);
        for (int i = 0; i < block; ++i) { o.L.push_back (buf.getSample (0, i)); o.R.push_back (buf.getSample (1, i)); }
        if (sleepMs) Thread::sleep (sleepMs);
    }
    return o;
}
static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += v[i] * v[i]; return std::sqrt (s / (double) (b - a)); }
static double tailDb (const Out& o, float amp) { const size_t n = o.L.size(); return 20 * std::log10 (rms (o.L, n * 3 / 4, n) / (amp / std::sqrt (2.0))); }

static int fails = 0;
static void check (const char* name, double got, double want, double tol)
{
    const bool ok = std::abs (got - want) <= tol; if (! ok) ++fails;
    std::printf ("%-52s got %8.3f  want %8.3f  %s\n", name, got, want, ok ? "ok" : "FAIL");
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    const double fs = 48000;
    EqProcessor p;
    p.setPlayConfigDetails (2, 2, fs, 256);
    p.prepareToPlay (fs, 256);

    reset (p);
    check ("bypass (no bands) = 0 dB", tailDb (run (p, 1000, .1f, 0, 1, fs), .1f), 0, .01);

    reset (p); band (p, 1, 0, 1000, 12, 1);
    check ("bell +12 dB @1k", tailDb (run (p, 1000, .1f, 0, 1, fs), .1f), 12, .1);

    reset (p); band (p, 1, 3, 1000, 0, .7071f, 0, 3);   // 24 dB/oct low cut
    check ("low cut 24dB/oct @500Hz (-24.1)", tailDb (run (p, 500, .1f, 0, 1, fs), .1f), -24.1, .5);

    reset (p); band (p, 1, 0, 1000, 12, 1, 3);           // mid only
    check ("bell +12 on Mid, mid signal", tailDb (run (p, 1000, .1f, 0, 1, fs), .1f), 12, .1);
    check ("bell +12 on Mid, side signal (untouched)", tailDb (run (p, 1000, .1f, 1, 1, fs), .1f), 0, .1);

    // smoothing: jump gain +12 -> -12 mid stream, output must never jump
    reset (p); band (p, 1, 0, 1000, 12, 1);
    run (p, 1000, .1f, 0, .3, fs);
    setP (p, "b1_gain", -12);
    auto o = run (p, 1000, .1f, 0, .5, fs);
    double maxStep = 0; for (size_t i = 1; i < o.L.size(); ++i) maxStep = std::max (maxStep, (double) std::abs (o.L[i] - o.L[i - 1]));
    check ("gain jump settles to -12 dB", tailDb (o, .1f), -12, .2);
    check ("max sample step stays small (no click)", maxStep < 0.4 ? 1 : 0, 1, 0);

    // dynamics: compress 12 dB above threshold
    reset (p); band (p, 1, 0, 1000, 0, 1);
    setP (p, "b1_dyn", 1); setP (p, "b1_dr", -12); setP (p, "b1_dthr", -30); setP (p, "b1_datt", 5); setP (p, "b1_drel", 50);
    check ("dynamic: loud signal compressed by 12 dB", tailDb (run (p, 1000, .5f, 0, 1, fs), .5f), -12, .5);
    check ("dynamic: quiet signal untouched", tailDb (run (p, 1000, .001f, 0, 1.5, fs), .001f), 0, .3);
    setP (p, "b1_ddir", 1);
    check ("dynamic (below): loud untouched", tailDb (run (p, 1000, .5f, 0, 1, fs), .5f), 0, .3);
    check ("dynamic (below): quiet cut by 12 dB", tailDb (run (p, 1000, .001f, 0, 1.5, fs), .001f), -12, .5);

    // linear phase
    reset (p); band (p, 1, 0, 1000, 12, 1);
    setP (p, "phase", 1); setP (p, "lquality", 0);
    p.prepareToPlay (fs, 256);
    check ("linear phase latency (2048 taps -> 1024)", p.getLatencySamples(), 1024, 0);
    run (p, 1000, .1f, 0, .3, fs, 4);
    Thread::sleep (800);
    o = run (p, 1000, .1f, 0, 2.0, fs, 4);
    check ("linear: bell +12 dB @1k", tailDb (o, .1f), 12, .3);
    {
        double err = 0, ref = 0; const size_t a = o.L.size() * 3 / 4, b = o.L.size();
        for (size_t i = a; i < b; ++i)
        {
            const double exp = 4.0 * .1 * std::sin (2 * MathConstants<double>::pi * 1000 * ((double) i - 1024) / fs);
            err += (o.L[i] - exp) * (o.L[i] - exp); ref += exp * exp;
        }
        check ("linear: output = 4x input delayed 1024 smp (err)", std::sqrt (err / ref) < 0.03 ? 1 : 0, 1, 0);
    }

    reset (p); band (p, 1, 0, 1000, 12, 1, 3);
    Thread::sleep (300);
    run (p, 1000, .1f, 0, .5, fs, 4); Thread::sleep (800);
    check ("linear Mid: mid signal +12", tailDb (run (p, 1000, .1f, 0, 1.5, fs, 4), .1f), 12, .4);
    check ("linear Mid: side signal 0 dB", tailDb (run (p, 1000, .1f, 1, 1.5, fs, 4), .1f), 0, .4);

    std::printf ("\n%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails;
}
