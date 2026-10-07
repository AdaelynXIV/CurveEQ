// Renders the editor to PNG files (dev tool, needs a display such as Xvfb).
#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace juce;

static void pump (int ms) { Timer::callAfterDelay (ms, [] { MessageManager::getInstance()->stopDispatchLoop(); }); MessageManager::getInstance()->runDispatchLoop(); }
static void setP (EqProcessor& p, const String& id, float plain)
{ auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (plain)); }
static void band (EqProcessor& p, int b, int type, float f, float g, float q, int mode = 0, int slope = 1)
{
    String s = "b" + String (b) + "_";
    setP (p, s + "on", 1); setP (p, s + "type", (float) type); setP (p, s + "freq", f); setP (p, s + "gain", g);
    setP (p, s + "q", q); setP (p, s + "mode", (float) mode); setP (p, s + "slope", (float) slope);
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const double fs = 48000;
    EqProcessor p;
    p.setPlayConfigDetails (2, 2, fs, 512);
    p.prepareToPlay (fs, 512);

    const bool alt = argc > 1;
    auto I = [&] (int n) { return alt ? (n == 1 ? 4 : n == 4 ? 1 : n) : n; };
    band (p, I(1), 3, 28, 0, .71f, 0, 3);          // low cut
    band (p, I(2), 1, 120, 3.5f, .71f);            // low shelf
    band (p, I(3), 0, 320, -4.2f, 2.1f);           // bell
    band (p, I(4), 0, 2800, 5.0f, 1.4f);           // bell, dynamic
    { String k = "b" + String (I(4)) + "_"; setP (p, k + "dyn", 1); setP (p, k + "dr", -6); setP (p, k + "dthr", -45); }
    band (p, I(5), 5, 7200, 0, 12.f);              // notch
    band (p, I(6), 2, 11000, 4.0f, .71f);          // high shelf

    // pink-ish noise with some tonal peaks so the analyser has something to show
    Random rnd (7);
    AudioBuffer<float> buf (2, 512); MidiBuffer mb;
    double b0 = 0, b1 = 0, b2 = 0;
    for (int blk = 0; blk < 200; ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double w = rnd.nextDouble() * 2 - 1;
            b0 = .99765 * b0 + w * .0990460; b1 = .96300 * b1 + w * .2965164; b2 = .57000 * b2 + w * 1.0526913;
            const double t = (blk * 512 + i) / fs;
            const float x = (float) ((b0 + b1 + b2 + w * .1848) * .08 + .05 * std::sin (6.2831853 * 110 * t) + .03 * std::sin (6.2831853 * 2800 * t));
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        p.processBlock (buf, mb);
    }

    std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
    ed->setBounds (0, 0, 1080, 700);
    pump (400);

    auto save = [&] (const char* name)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        File f (String ("/home/claude/") + name);
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (img, os);
    };
    save (alt ? "shot2.png" : "shot1.png");

    return 0;
}
