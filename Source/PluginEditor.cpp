#include "PluginEditor.h"

using namespace juce;

static String pid (int b, const char* n) { return "b" + String (b + 1) + "_" + n; }
static constexpr float kFMin = 10.f, kFMax = 30000.f;
static constexpr float kAnTop = -10.f, kAnBot = -100.f;   // analyser display range (dBFS)

static Colour bandColour (int i)
{
    static const uint32 pal[] = { 0xffe8564f, 0xffef8a3c, 0xfff2c744, 0xff9bd24a, 0xff4cc47a, 0xff3fc7c0,
                                  0xff4aa8ee, 0xff6b7ff0, 0xffa069ee, 0xffd964d1, 0xffee6a9b, 0xffb4c0cc };
    return Colour (pal[i % 12]);
}

static String freqText (float f) { return f >= 1000.f ? String (f / 1000.f, 2) + " kHz" : String (f, f < 100.f ? 1 : 0) + " Hz"; }

static String noteText (float f)
{
    static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float m = 69.f + 12.f * std::log2 (f / 440.f);
    const int n = (int) std::round (m), cents = (int) std::round ((m - (float) n) * 100.f);
    return String (nm[((n % 12) + 12) % 12]) + String (n / 12 - 1) + (cents >= 0 ? " +" : " ") + String (cents) + "c";
}

// ------------------------------------------------------------------ look and feel
ProLook::ProLook()
{
    setColour (ComboBox::textColourId, Colour (0xffdde3ea));
    setColour (PopupMenu::backgroundColourId, Colour (0xff22272e));
    setColour (PopupMenu::textColourId, Colour (0xffdde3ea));
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff3a4350));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (Slider::textBoxTextColourId, Colour (0xffe3e8ee));
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::rotarySliderFillColourId, Colour (0xff4aa8ee));
}

void ProLook::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.f);
    const float R = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float al = s.isEnabled() ? 1.f : 0.3f;
    const float ang = a0 + pos * (a1 - a0);
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float startA = bipolar ? a0 + (a1 - a0) * (float) s.valueToProportionOfLength (0.0) : a0;

    Path track; track.addCentredArc (c.x, c.y, R - 3.f, R - 3.f, 0.f, a0, a1, true);
    g.setColour (Colour (0xff343a44).withAlpha (al));
    g.strokePath (track, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    Path val; val.addCentredArc (c.x, c.y, R - 3.f, R - 3.f, 0.f, jmin (startA, ang), jmax (startA, ang), true);
    g.setColour (s.findColour (Slider::rotarySliderFillColourId).withAlpha (al));
    g.strokePath (val, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    const float br = R - 10.f;
    g.setGradientFill (ColourGradient (Colour (0xff434a56).withAlpha (al), c.x, c.y - br, Colour (0xff23282f).withAlpha (al), c.x, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f);
    g.setColour (Colour (0xff12151a).withAlpha (al));
    g.drawEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f, 1.f);
    g.setColour (Colours::white.withAlpha (al));
    g.drawLine (Line<float> (c.getPointOnCircumference (br * 0.38f, ang), c.getPointOnCircumference (br - 2.5f, ang)), 2.2f);
}

void ProLook::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
    const float al = box.isEnabled() ? 1.f : 0.45f;
    g.setColour (Colour (0xff2a2f37).withAlpha (al));  g.fillRoundedRectangle (r, 5.f);
    g.setColour (Colour (0xff3b424d).withAlpha (al));  g.drawRoundedRectangle (r, 5.f, 1.f);
    Path a; const float cx = (float) w - 12.f, cy = (float) h * 0.5f;
    a.addTriangle (cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.f);
    g.setColour (Colour (0xff9aa6b4).withAlpha (al)); g.fillPath (a);
}

Font ProLook::getComboBoxFont (ComboBox&) { return Font (FontOptions (13.f)); }
void ProLook::positionComboBoxText (ComboBox& box, Label& l)
{
    l.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
    l.setFont (getComboBoxFont (box));
}

void ProLook::drawToggleButton (Graphics& g, ToggleButton& b, bool, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float pw = 30.f, ph = 16.f, al = b.isEnabled() ? 1.f : 0.4f;
    const Rectangle<float> pill (r.getX(), r.getCentreY() - ph * 0.5f, pw, ph);
    const bool isOn = b.getToggleState();
    g.setColour (isOn ? b.findColour (ToggleButton::tickColourId).withAlpha (al) : Colour (0xff3a414c).withAlpha (al));
    g.fillRoundedRectangle (pill, ph * 0.5f);
    g.setColour (Colours::white.withAlpha (al));
    g.fillEllipse (isOn ? pill.getRight() - ph + 2.f : pill.getX() + 2.f, pill.getY() + 2.f, ph - 4.f, ph - 4.f);
    g.setColour (Colour (0xffc8d0da).withAlpha (al));
    g.setFont (12.5f);
    g.drawText (b.getButtonText(), r.withTrimmedLeft (pw + 7.f), Justification::centredLeft);
}

// ------------------------------------------------------------------ editor
EqEditor::EqEditor (EqProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    setSize (1080, 700);
    setResizable (true, true);
    setResizeLimits (1080, 620, 1900, 1200);
    setWantsKeyboardFocus (true);
    spectrum.fill (-120.f);

    type.addItemList ({ "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" }, 1);
    slope.addItemList ({ "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" }, 1);
    mode.addItemList ({ "Stereo", "Left", "Right", "Mid", "Side" }, 1);
    dir.addItemList ({ "Above", "Below" }, 1);
    phase.addItemList ({ "Zero Latency", "Linear Phase" }, 1);
    quality.addItemList ({ "Low", "Medium", "High", "Max" }, 1);
    range.addItemList ({ "6 dB", "12 dB", "18 dB", "30 dB" }, 1);
    range.setSelectedId (3, dontSendNotification);
    range.onChange = [this] { const float r[] = { 6.f, 12.f, 18.f, 30.f }; dbRange = r[range.getSelectedId() - 1]; };

    auto knob = [this] (Slider& s)
    {
        s.setSliderStyle (Slider::RotaryVerticalDrag);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setTextBoxStyle (Slider::TextBoxBelow, false, 78, 18);
        s.setDoubleClickReturnValue (true, s.getValue());
        addAndMakeVisible (s);
    };
    for (auto* s : { &freq, &gain, &q, &dRange, &dThr, &dAtt, &dRel, &outGain }) knob (*s);
    outGain.setColour (Slider::rotarySliderFillColourId, Colour (0xff8ea0b5));
    for (auto* c : { &type, &slope, &mode, &dir, &phase, &quality, &range }) addAndMakeVisible (c);
    for (auto* t : { &on, &dyn, &analyzer }) addAndMakeVisible (t);
    dyn.setColour (ToggleButton::tickColourId, Colour (0xff4aa8ee));
    analyzer.setColour (ToggleButton::tickColourId, Colour (0xff4aa8ee));
    analyzer.setToggleState (true, dontSendNotification);

    aOut     = std::make_unique<APVTS::SliderAttachment> (proc.apvts, "out_gain", outGain);
    aPhase   = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "phase", phase);
    aQuality = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "lquality", quality);
    selectBand (0);
    startTimerHz (30);
}

EqEditor::~EqEditor() { setLookAndFeel (nullptr); }

void EqEditor::selectBand (int i)
{
    sel = i;
    aType.reset(); aSlope.reset(); aMode.reset(); aDir.reset();
    aFreq.reset(); aGain.reset(); aQ.reset(); aRange.reset(); aThr.reset(); aAtt.reset(); aRel.reset();
    aOn.reset(); aDyn.reset();
    auto& v = proc.apvts;
    aType  = std::make_unique<APVTS::ComboBoxAttachment> (v, pid (i, "type"), type);
    aSlope = std::make_unique<APVTS::ComboBoxAttachment> (v, pid (i, "slope"), slope);
    aMode  = std::make_unique<APVTS::ComboBoxAttachment> (v, pid (i, "mode"), mode);
    aDir   = std::make_unique<APVTS::ComboBoxAttachment> (v, pid (i, "ddir"), dir);
    aFreq  = std::make_unique<APVTS::SliderAttachment> (v, pid (i, "freq"), freq);
    aGain  = std::make_unique<APVTS::SliderAttachment> (v, pid (i, "gain"), gain);
    aQ     = std::make_unique<APVTS::SliderAttachment> (v, pid (i, "q"), q);
    aRange = std::make_unique<APVTS::SliderAttachment> (v, pid (i, "dr"), dRange);
    aThr   = std::make_unique<APVTS::SliderAttachment> (v, pid (i, "dthr"), dThr);
    aAtt   = std::make_unique<APVTS::SliderAttachment> (v, pid (i, "datt"), dAtt);
    aRel   = std::make_unique<APVTS::SliderAttachment> (v, pid (i, "drel"), dRel);
    aOn    = std::make_unique<APVTS::ButtonAttachment> (v, pid (i, "on"), on);
    aDyn   = std::make_unique<APVTS::ButtonAttachment> (v, pid (i, "dyn"), dyn);

    const auto c = bandColour (i);
    for (auto* s : { &freq, &gain, &q, &dRange, &dThr, &dAtt, &dRel }) s->setColour (Slider::rotarySliderFillColourId, c);
    on.setColour (ToggleButton::tickColourId, c);
    repaint();
}

float EqEditor::get (int b, const char* id) const { return proc.apvts.getRawParameterValue (pid (b, id))->load(); }

void EqEditor::setParam (int b, const char* id, float plain)
{
    if (auto* p = proc.apvts.getParameter (pid (b, id))) p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void EqEditor::gesture (int b, const char* id, bool begin)
{
    if (auto* p = proc.apvts.getParameter (pid (b, id))) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
}

float EqEditor::freqToX (float f) const { return plot.getX() + std::log (f / kFMin) / std::log (kFMax / kFMin) * plot.getWidth(); }
float EqEditor::xToFreq (float x) const { return kFMin * std::pow (kFMax / kFMin, jlimit (0.f, 1.f, (x - plot.getX()) / plot.getWidth())); }
float EqEditor::dbToY (float d) const { return plot.getY() + (1.f - (d + dbRange) / (2.f * dbRange)) * plot.getHeight(); }
float EqEditor::yToDb (float y) const { return ((1.f - (y - plot.getY()) / plot.getHeight()) * 2.f - 1.f) * dbRange; }

void EqEditor::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (48);
    auto bottom = r.removeFromBottom (182);
    plot = r.toFloat().withTrimmedLeft (46).withTrimmedRight (46).withTrimmedTop (12).withTrimmedBottom (40);

    // header controls, right aligned
    auto h = header.reduced (16, 10);
    range.setBounds (h.removeFromRight (80));          h.removeFromRight (8);
    analyzer.setBounds (h.removeFromRight (96));       h.removeFromRight (8);
    quality.setBounds (h.removeFromRight (90));        h.removeFromRight (8);
    phase.setBounds (h.removeFromRight (130));

    auto b = bottom.reduced (12, 8);
    bandGroup = b.removeFromLeft (420); b.removeFromLeft (10);
    dynGroup  = b.removeFromLeft (480); b.removeFromLeft (10);
    outGroup  = b;
    knobLabels.clear();

    auto slot = [this] (Slider& s, Rectangle<int> a, const String& name)
    {
        knobLabels.push_back ({ name, a.removeFromTop (16) });
        s.setBounds (a);
    };

    {   // band group
        auto in = bandGroup.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (8);
        on.setBounds (bandGroup.getRight() - 64, bandGroup.getY() + 6, 56, 20);
        auto col = in.removeFromLeft (118);
        type.setBounds (col.removeFromTop (28)); col.removeFromTop (6);
        mode.setBounds (col.removeFromTop (28)); col.removeFromTop (6);
        slope.setBounds (col.removeFromTop (28));
        in.removeFromLeft (6);
        const int w = in.getWidth() / 3;
        slot (freq, in.removeFromLeft (w), "Frequency");
        slot (gain, in.removeFromLeft (w), "Gain");
        slot (q, in, "Q");
    }
    {   // dynamics group
        auto in = dynGroup.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (8);
        dyn.setBounds (dynGroup.getRight() - 104, dynGroup.getY() + 6, 96, 20);
        auto col = in.removeFromLeft (92);
        col.removeFromTop (16);
        dir.setBounds (col.removeFromTop (28));
        knobLabels.push_back ({ "Trigger", Rectangle<int> (col.getX(), col.getY() - 44, col.getWidth(), 16) });
        in.removeFromLeft (6);
        const int w = in.getWidth() / 4;
        slot (dRange, in.removeFromLeft (w), "Range");
        slot (dThr, in.removeFromLeft (w), "Threshold");
        slot (dAtt, in.removeFromLeft (w), "Attack");
        slot (dRel, in, "Release");
    }
    {   // output group
        auto in = outGroup.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (8);
        slot (outGain, in, "Gain");
    }
}

// ------------------------------------------------------------------ interaction
int EqEditor::hitTest (Point<float> pt) const
{
    int best = -1; float bd = 15.f;
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        if (get (b, "on") < 0.5f) continue;
        const int t = (int) get (b, "type");
        const Point<float> c (freqToX (get (b, "freq")), dbToY (eq::hasGain (t) ? get (b, "gain") : 0.f));
        const float d = c.getDistanceFrom (pt);
        if (d < bd) { bd = d; best = b; }
    }
    return best;
}

int EqEditor::findFreeBand() const
{
    for (int b = 0; b < eq::kNumBands; ++b) if (get (b, "on") < 0.5f) return b;
    return -1;
}

void EqEditor::removeBand (int b)
{
    gesture (b, "on", true); setParam (b, "on", 0.f); gesture (b, "on", false);
}

void EqEditor::showBandMenu (int b)
{
    PopupMenu m;
    const char* names[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" };
    const int cur = (int) get (b, "type");
    for (int i = 0; i < 7; ++i) m.addItem (i + 1, names[i], true, i == cur);
    m.addSeparator();
    m.addItem (100, "Delete band");
    Component::SafePointer<EqEditor> safe (this);
    m.showMenuAsync (PopupMenu::Options(), [safe, b] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 100) { safe->removeBand (b); return; }
        safe->gesture (b, "type", true); safe->setParam (b, "type", (float) (r - 1)); safe->gesture (b, "type", false);
        if (r - 1 != 0 && r - 1 != 5 && r - 1 != 6) safe->setParam (b, "q", 0.71f);
    });
}

void EqEditor::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    const int h = hitTest (e.position);
    if (h < 0) return;
    selectBand (h);
    if (e.mods.isPopupMenu()) { showBandMenu (h); return; }
    dragging = h;
    const int t = (int) get (h, "type");
    dragMouse = e.position;
    dragNode = { freqToX (get (h, "freq")), dbToY (eq::hasGain (t) ? get (h, "gain") : 0.f) };
    gesture (h, "freq", true);
    if (eq::hasGain (t)) gesture (h, "gain", true);
}

void EqEditor::mouseDrag (const MouseEvent& e)
{
    hover = e.position;
    if (dragging < 0) return;
    const float k = e.mods.isShiftDown() ? 0.1f : 1.f; // shift = fine adjust
    const auto p = dragNode + (e.position - dragMouse) * k;
    setParam (dragging, "freq", jlimit (kFMin, kFMax, xToFreq (p.x)));
    if (eq::hasGain ((int) get (dragging, "type")))
        setParam (dragging, "gain", jlimit (-30.f, 30.f, yToDb (p.y)));
}

void EqEditor::mouseUp (const MouseEvent&)
{
    if (dragging < 0) return;
    gesture (dragging, "freq", false);
    if (eq::hasGain ((int) get (dragging, "type"))) gesture (dragging, "gain", false);
    dragging = -1;
}

void EqEditor::mouseMove (const MouseEvent& e) { hover = e.position; hoverBand = hitTest (e.position); }
void EqEditor::mouseExit (const MouseEvent&) { hover = { -1.f, -1.f }; hoverBand = -1; }

void EqEditor::mouseDoubleClick (const MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h >= 0) { removeBand (h); return; }
    if (! plot.contains (e.position)) return;
    const int b = findFreeBand();
    if (b < 0) return;
    setParam (b, "type", 0.f); setParam (b, "mode", 0.f); setParam (b, "slope", 1.f); setParam (b, "q", 1.f);
    setParam (b, "dyn", 0.f);
    setParam (b, "freq", xToFreq (e.position.x));
    setParam (b, "gain", jlimit (-30.f, 30.f, yToDb (e.position.y)));
    setParam (b, "on", 1.f);
    selectBand (b);
}

void EqEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    int h = hitTest (e.position);
    if (h < 0 && plot.contains (e.position)) h = (get (sel, "on") > 0.5f) ? sel : -1;
    if (h < 0) return;
    const float nq = jlimit (0.1f, 40.f, get (h, "q") * std::exp (w.deltaY * 1.5f));
    gesture (h, "q", true); setParam (h, "q", nq); gesture (h, "q", false);
}

bool EqEditor::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey) { removeBand (sel); return true; }
    return false;
}

void EqEditor::timerCallback()
{
    const bool bandOn = get (sel, "on") > 0.5f;
    const int t = (int) get (sel, "type");
    const bool dynOk = bandOn && (t == eq::Bell || t == eq::LowShelf || t == eq::HighShelf);
    const bool dynOn = dynOk && get (sel, "dyn") > 0.5f;
    for (auto* c : { (Component*) &type, (Component*) &mode, (Component*) &freq, (Component*) &q }) c->setEnabled (bandOn);
    gain.setEnabled (bandOn && eq::hasGain (t));
    slope.setEnabled (bandOn && eq::isCut (t));
    dyn.setEnabled (dynOk);
    for (auto* c : { (Component*) &dir, (Component*) &dRange, (Component*) &dThr, (Component*) &dAtt, (Component*) &dRel }) c->setEnabled (dynOn);
    quality.setVisible (proc.isLinearPhase());

    proc.copyLatest (fftIn.data(), kFft);
    window.multiplyWithWindowingTable (fftIn.data(), kFft);
    std::fill (fftData.begin(), fftData.end(), 0.f);
    std::copy (fftIn.begin(), fftIn.end(), fftData.begin());
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    for (size_t k = 0; k < spectrum.size(); ++k)
    {
        const float db = Decibels::gainToDecibels (fftData[k] / (kFft * 0.25f), -140.f);
        spectrum[k] = db > spectrum[k] ? 0.55f * db + 0.45f * spectrum[k] : 0.93f * spectrum[k] + 0.07f * db;
    }
    repaint();
}

// ------------------------------------------------------------------ drawing
void EqEditor::drawPiano (Graphics& g, Rectangle<float> r) const
{
    static const bool black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    g.setColour (Colour (0xff101317)); g.fillRoundedRectangle (r, 2.f);
    auto keyRange = [] (int midi, float& f0, float& f1)
    {
        f0 = 440.f * std::pow (2.f, ((float) midi - 69.5f) / 12.f);
        f1 = 440.f * std::pow (2.f, ((float) midi - 68.5f) / 12.f);
    };
    for (int pass = 0; pass < 2; ++pass)
        for (int m = 0; m < 128; ++m)
        {
            float f0, f1; keyRange (m, f0, f1);
            if (f1 < kFMin || f0 > kFMax || black[m % 12] != (pass == 1)) continue;
            const float x0 = freqToX (jmax (f0, kFMin)), x1 = freqToX (jmin (f1, kFMax));
            if (pass == 0)
            {
                g.setColour (Colour (0xff6b7480));
                g.fillRect (x0, r.getY(), jmax (0.5f, x1 - x0 - 0.7f), r.getHeight());
            }
            else
            {
                g.setColour (Colour (0xff14171b));
                g.fillRect (x0, r.getY(), jmax (0.5f, x1 - x0), r.getHeight() * 0.62f);
            }
        }
}

void EqEditor::drawTooltip (Graphics& g, int b) const
{
    const int t = (int) get (b, "type");
    const float f = get (b, "freq");
    const char* names[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" };
    StringArray lines;
    lines.add ("Band " + String (b + 1) + "  " + names[t]);
    lines.add (freqText (f) + "   " + noteText (f));
    String l3;
    if (eq::hasGain (t)) l3 << (get (b, "gain") > 0 ? "+" : "") << String (get (b, "gain"), 1) << " dB   ";
    l3 << "Q " << String (get (b, "q"), 2);
    lines.add (l3);

    const Point<float> c (freqToX (f), dbToY (eq::hasGain (t) ? get (b, "gain") : 0.f));
    Rectangle<float> box (c.x + 16.f, c.y - 62.f, 150.f, 54.f);
    if (box.getRight() > plot.getRight() - 4.f) box.setX (c.x - 16.f - box.getWidth());
    if (box.getY() < plot.getY() + 4.f) box.setY (c.y + 16.f);
    g.setColour (Colour (0xe61a1d23)); g.fillRoundedRectangle (box, 6.f);
    g.setColour (bandColour (b).withAlpha (0.8f)); g.drawRoundedRectangle (box, 6.f, 1.2f);
    g.setFont (Font (FontOptions (12.f, Font::bold)));
    g.setColour (bandColour (b)); g.drawText (lines[0], box.reduced (9.f, 4.f).removeFromTop (16.f), Justification::left);
    g.setFont (12.f); g.setColour (Colour (0xffe3e8ee));
    g.drawText (lines[1], box.reduced (9.f, 4.f).withTrimmedTop (16.f).removeFromTop (16.f), Justification::left);
    g.drawText (lines[2], box.reduced (9.f, 4.f).withTrimmedTop (32.f), Justification::left);
}

void EqEditor::paint (Graphics& g)
{
    const double fs = std::max (44100.0, proc.getSampleRate());
    g.fillAll (Colour (0xff14171c));

    // ---- header
    g.setColour (Colour (0xff1a1d23)); g.fillRect (header);
    g.setColour (Colour (0xff262b33)); g.drawHorizontalLine (header.getBottom() - 1, 0.f, (float) getWidth());
    {
        const Rectangle<float> logo (18.f, 11.f, 26.f, 26.f);
        g.setGradientFill (ColourGradient (Colour (0xfff5c04a), logo.getX(), logo.getY(), Colour (0xffe8564f), logo.getRight(), logo.getBottom(), false));
        g.fillRoundedRectangle (logo, 7.f);
        Path w; w.startNewSubPath (22.f, 26.f);
        w.cubicTo (28.f, 14.f, 33.f, 14.f, 36.f, 24.f); w.cubicTo (38.f, 30.f, 40.f, 28.f, 41.f, 24.f);
        g.setColour (Colour (0xff14171c)); g.strokePath (w, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colours::white); g.setFont (Font (FontOptions (20.f, Font::bold)));
        g.drawText ("Curve EQ", 54, 0, 160, header.getHeight(), Justification::centredLeft);
    }

    // ---- plot background
    g.setGradientFill (ColourGradient (Colour (0xff262b33), plot.getCentreX(), plot.getY(), Colour (0xff1b1f25), plot.getCentreX(), plot.getBottom(), false));
    g.fillRoundedRectangle (plot, 6.f);

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    // grid
    const float step = dbRange <= 6.f ? 2.f : dbRange <= 12.f ? 3.f : dbRange <= 18.f ? 6.f : 10.f;
    for (float d = -dbRange; d <= dbRange + 0.01f; d += step)
    {
        g.setColour (d == 0.f ? Colour (0xff4a525d) : Colour (0xff2a3038));
        g.drawHorizontalLine ((int) dbToY (d), plot.getX(), plot.getRight());
    }
    for (float f : { 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 90.f, 100.f, 200.f, 300.f, 400.f, 500.f, 600.f, 700.f, 800.f, 900.f, 1000.f,
                     2000.f, 3000.f, 4000.f, 5000.f, 6000.f, 7000.f, 8000.f, 9000.f, 10000.f, 20000.f })
    {
        const bool major = f == 20.f || f == 100.f || f == 1000.f || f == 10000.f || f == 50.f || f == 500.f || f == 5000.f || f == 200.f || f == 2000.f || f == 20000.f;
        g.setColour (Colour (major ? 0xff2d343d : 0xff242930));
        g.drawVerticalLine ((int) freqToX (f), plot.getY(), plot.getBottom());
    }

    // spectrum
    if (analyzer.getToggleState())
    {
        Path top;
        const float binHz = (float) fs / kFft, nyq = (float) fs * 0.5f;
        bool started = false;
        for (float x = plot.getX(); x <= plot.getRight() + 1.f; x += 2.f)
        {
            const float f0 = xToFreq (x - 1.5f), f1 = xToFreq (x + 1.5f), fc = xToFreq (x);
            float db = -140.f;
            if (f0 < nyq)
            {
                const float lo = f0 / binHz, hi = f1 / binHz;
                if (hi - lo < 1.f)
                {
                    const float pos = 0.5f * (lo + hi);
                    const int k = jlimit (0, (int) spectrum.size() - 2, (int) pos);
                    db = jmap (pos - (float) k, spectrum[(size_t) k], spectrum[(size_t) k + 1]);
                }
                else
                {
                    float mx = -140.f, sum = 0.f; int cnt = 0;
                    for (int k = jmax (1, (int) lo); k <= jmin ((int) spectrum.size() - 1, (int) hi); ++k) { mx = jmax (mx, spectrum[(size_t) k]); sum += spectrum[(size_t) k]; ++cnt; }
                    db = cnt ? 0.5f * mx + 0.5f * sum / (float) cnt : -140.f;
                }
            }
            db += 4.5f * std::log2 (fc / 1000.f); // +4.5 dB/oct tilt so pink noise reads flat
            const float y = plot.getBottom() - jlimit (0.f, 1.f, (db - kAnBot) / (kAnTop - kAnBot)) * plot.getHeight();
            if (! started) { top.startNewSubPath (x, y); started = true; } else top.lineTo (x, y);
        }
        Path fill (top);
        fill.lineTo (plot.getRight() + 1.f, plot.getBottom() + 1.f); fill.lineTo (plot.getX(), plot.getBottom() + 1.f); fill.closeSubPath();
        g.setGradientFill (ColourGradient (Colour (0xa04a88ff), 0.f, plot.getY(), Colour (0x182f48a8), 0.f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (Colour (0xb08cbcff));
        g.strokePath (top, PathStrokeType (1.2f));
    }

    // pre-compute curve positions
    std::vector<float> xs; std::vector<std::complex<double>> z1s;
    for (float x = plot.getX(); x <= plot.getRight() + 0.5f; x += 2.f)
    {
        xs.push_back (x);
        z1s.push_back (std::polar (1.0, -2.0 * 3.14159265358979 * xToFreq (x) / fs));
    }
    auto dbAt = [&] (const eq::Sections& s, size_t i) { return 20.0 * std::log10 (std::max (eq::magnitude (s, z1s[i]), 1e-9)); };
    auto yOf = [&] (double db) { return dbToY (jlimit (-dbRange, dbRange, (float) db)); };

    std::array<eq::Sections, eq::kNumBands> live;
    std::vector<double> tot (xs.size(), 0.0);
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        if (get (b, "on") < 0.5f) continue;
        const int t = (int) get (b, "type");
        double gn = get (b, "gain");
        const bool d = get (b, "dyn") > 0.5f && eq::hasGain (t);
        if (d)
        {
            // dashed ghost = where the band goes at full dynamic range
            const auto ghost = eq::design (t, fs, get (b, "freq"), gn + get (b, "dr"), get (b, "q"), (int) get (b, "slope"));
            Path gp;
            for (size_t i = 0; i < xs.size(); ++i) { const float y = yOf (dbAt (ghost, i)); i ? gp.lineTo (xs[i], y) : gp.startNewSubPath (xs[i], y); }
            Path dashed; const float dl[2] = { 4.f, 4.f };
            PathStrokeType (1.3f).createDashedStroke (dashed, gp, dl, 2);
            g.setColour (bandColour (b).withAlpha (b == sel ? 0.8f : 0.4f)); g.fillPath (dashed);
            gn += (double) get (b, "dr") * proc.getDynAmt (b);
        }
        live[(size_t) b] = eq::design (t, fs, get (b, "freq"), gn, get (b, "q"), (int) get (b, "slope"));
        for (size_t i = 0; i < xs.size(); ++i) tot[i] += dbAt (live[(size_t) b], i);
    }

    // selected band shape
    if (live[(size_t) sel].n > 0)
    {
        Path bp; bp.startNewSubPath (xs.front(), dbToY (0.f));
        for (size_t i = 0; i < xs.size(); ++i) bp.lineTo (xs[i], yOf (dbAt (live[(size_t) sel], i)));
        Path line (bp);
        bp.lineTo (xs.back(), dbToY (0.f)); bp.closeSubPath();
        g.setColour (bandColour (sel).withAlpha (0.24f)); g.fillPath (bp);
        g.setColour (bandColour (sel).withAlpha (0.9f)); g.strokePath (line, PathStrokeType (1.4f));
    }

    // total curve
    {
        Path tp, fillp;
        fillp.startNewSubPath (xs.front(), dbToY (0.f));
        for (size_t i = 0; i < xs.size(); ++i)
        {
            const float y = yOf (tot[i]);
            if (i) tp.lineTo (xs[i], y); else tp.startNewSubPath (xs[i], y);
            fillp.lineTo (xs[i], y);
        }
        fillp.lineTo (xs.back(), dbToY (0.f)); fillp.closeSubPath();
        g.setColour (Colour (0x14f5c04a)); g.fillPath (fillp);
        g.setColour (Colour (0x30f5c04a)); g.strokePath (tp, PathStrokeType (7.f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xfff7cf63)); g.strokePath (tp, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // cursor readout
    if (dragging < 0 && hoverBand < 0 && plot.contains (hover))
    {
        const float f = xToFreq (hover.x);
        const String s = freqText (f) + "    " + noteText (f) + "    " + String (yToDb (hover.y), 1) + " dB";
        g.setFont (12.f);
        const Rectangle<float> r (plot.getX() + 10.f, plot.getY() + 8.f, 22.f + (float) s.length() * 6.8f, 22.f);
        g.setColour (Colour (0xb8101317)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (Colour (0xffd5dbe3)); g.drawText (s, r.reduced (9.f, 0.f), Justification::centredLeft);
    }

    // nodes
    g.setFont (Font (FontOptions (11.f, Font::bold)));
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        if (get (b, "on") < 0.5f) continue;
        const int t = (int) get (b, "type");
        const Point<float> c (freqToX (get (b, "freq")), dbToY (eq::hasGain (t) ? get (b, "gain") : 0.f));
        const float r = b == sel ? 10.5f : (b == hoverBand ? 10.f : 8.5f);
        if (b == sel || b == hoverBand) { g.setColour (bandColour (b).withAlpha (0.25f)); g.fillEllipse (c.x - r - 5.f, c.y - r - 5.f, 2.f * (r + 5.f), 2.f * (r + 5.f)); }
        g.setColour (bandColour (b)); g.fillEllipse (c.x - r, c.y - r, 2.f * r, 2.f * r);
        g.setColour (b == sel ? Colours::white : Colour (0xff14171c)); g.drawEllipse (c.x - r, c.y - r, 2.f * r, 2.f * r, b == sel ? 2.2f : 1.5f);
        g.setColour (Colour (0xff14171c)); g.drawText (String (b + 1), Rectangle<float> (c.x - r, c.y - r, 2.f * r, 2.f * r), Justification::centred);
    }
    g.restoreState();

    if (dragging >= 0) drawTooltip (g, dragging);
    else if (hoverBand >= 0) drawTooltip (g, hoverBand);

    // ---- axes
    g.setFont (11.f);
    for (float d = -dbRange; d <= dbRange + 0.01f; d += step)
    {
        g.setColour (d == 0.f ? Colour (0xffe6c36e) : Colour (0xff9c8a58));
        g.drawText ((d > 0 ? "+" : "") + String ((int) d), 0, (int) dbToY (d) - 7, (int) plot.getX() - 7, 14, Justification::right);
    }
    g.setColour (Colour (0xff5b7aa8));
    for (int a = -20; a >= -90; a -= 10)
    {
        const float y = plot.getBottom() - (a - kAnBot) / (kAnTop - kAnBot) * plot.getHeight();
        g.drawText (String (a), (int) plot.getRight() + 8, (int) y - 7, 36, 14, Justification::left);
    }
    g.setColour (Colour (0xff8a97a5));
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        g.drawText (f >= 1000.f ? String (f / 1000.f, 0) + "k" : String ((int) f), (int) freqToX (f) - 20, (int) plot.getBottom() + 4, 40, 14, Justification::centred);
    drawPiano (g, Rectangle<float> (plot.getX(), plot.getBottom() + 20.f, plot.getWidth(), 13.f));

    // ---- bottom panels
    auto group = [&] (Rectangle<int> r, const String& title, Colour accent)
    {
        g.setColour (Colour (0xff1c2026)); g.fillRoundedRectangle (r.toFloat(), 9.f);
        g.setColour (Colour (0xff2a3038)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 9.f, 1.f);
        g.setColour (accent); g.fillEllipse ((float) r.getX() + 12.f, (float) r.getY() + 11.f, 9.f, 9.f);
        g.setColour (Colour (0xffdbe1e8)); g.setFont (Font (FontOptions (13.f, Font::bold)));
        g.drawText (title, r.getX() + 28, r.getY() + 6, 200, 20, Justification::centredLeft);
    };
    group (bandGroup, "Band " + String (sel + 1), bandColour (sel));
    group (dynGroup, "Dynamics", Colour (0xff4aa8ee));
    group (outGroup, "Output", Colour (0xff8ea0b5));
    g.setFont (11.5f); g.setColour (Colour (0xff8d99a7));
    for (auto& kl : knobLabels) g.drawText (kl.first, kl.second, Justification::centred);
}
