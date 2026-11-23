#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "RecoilEngine.h"

// The shape library.
//   FACTORY: 16 familiar ducking and gate shapes (rows 1-2 ducking, rows 3-4 gates and swells)
//   RECOIL:  shapes that only make sense in a sidechain weapon
//   USER:    shapes you save from the editor, as small XML files next to the presets
// The "shape" parameter picks one of FACTORY + RECOIL, or "Custom": the nodes kept in the plugin state (child SHAPE).
namespace shapes
{
using rc::Node;

struct Entry { const char* name; const char* desc; std::vector<Node> nodes; };

inline const std::vector<Entry>& factory()
{
    static const std::vector<Entry> v {
        { "Punch-Through", "Eases into the duck over the first few ms instead of slamming, so the hit keeps its punch. The everyday choice.",
          { { 0, 0.2f }, { 0.07f, 0, 1.2f }, { 0.46f, 1, 2, true }, { 1, 1 } } },
        { "Hold", "Fully down for a moment, then a smooth return.",
          { { 0, 0 }, { 0.12f, 0 }, { 0.47f, 1, 2, true }, { 1, 1 } } },
        { "Tight", "Holds a little longer and snaps back quickly.",
          { { 0, 0 }, { 0.15f, 0 }, { 0.37f, 1, 2.6f, true }, { 1, 1 } } },
        { "Smooth", "A soft dip and an even return. Gentle on sustained sounds.",
          { { 0, 0.05f }, { 0.05f, 0, 1 }, { 0.47f, 1, 1.6f, true }, { 1, 1 } } },
        { "Punch-Through Long", "Punch-Through with a longer return, for slower tempos and long kicks.",
          { { 0, 0.2f }, { 0.07f, 0, 1.2f }, { 0.6f, 1, 2, true }, { 1, 1 } } },
        { "Hold Long", "A longer hold and a long return: the classic pump.",
          { { 0, 0 }, { 0.17f, 0 }, { 0.62f, 1, 2, true }, { 1, 1 } } },
        { "Snap", "Stays down, then snaps back hard.",
          { { 0, 0 }, { 0.21f, 0 }, { 0.35f, 1, 2.8f, true }, { 1, 1 } } },
        { "Quick", "A short, fast duck for busy patterns.",
          { { 0, 0.12f }, { 0.04f, 0, 1 }, { 0.22f, 1, 2.2f, true }, { 1, 1 } } },
        { "Swell Gate", "Silent on the hit, swells in, cuts before the next one.",
          { { 0, 0 }, { 0.08f, 0 }, { 0.4f, 1, -2.4f }, { 0.86f, 1 }, { 0.98f, 0, -2.6f } } },
        { "Cut", "Plays the start of each cycle, then cuts.",
          { { 0, 1 }, { 0.14f, 1 }, { 0.22f, 0, 0.4f }, { 1, 0 } } },
        { "Late Dip", "Untouched on the hit, dips late in the cycle.",
          { { 0, 1 }, { 0.3f, 1 }, { 0.66f, 0, -1.8f }, { 0.75f, 0 }, { 0.98f, 1, -2.4f } } },
        { "Spike", "Builds up to a peak late in the cycle.",
          { { 0, 0 }, { 0.3f, 0 }, { 0.68f, 1, -2.6f }, { 0.98f, 0 } } },
        { "Pulse", "A gate that opens halfway through the cycle.",
          { { 0, 0 }, { 0.38f, 0 }, { 0.45f, 1, 1.4f, true }, { 0.78f, 1 }, { 0.98f, 0, -2.2f } } },
        { "Fade Out", "Fades away over the cycle.",
          { { 0, 1 }, { 0.12f, 1 }, { 0.62f, 0, 1.7f, true }, { 1, 0 } } },
        { "Drop & Rise", "Drops away, then rises back into the next hit.",
          { { 0, 1 }, { 0.27f, 1 }, { 0.4f, 0, 1.6f, true }, { 0.72f, 0 }, { 0.98f, 1, -2.6f } } },
        { "Double", "A short blip, then a long rise: two pumps per cycle.",
          { { 0, 0 }, { 0.15f, 0.55f, 1.6f, true }, { 0.23f, 0, -1.4f }, { 0.33f, 0 }, { 0.98f, 1, 1.5f, true } } },
    };
    return v;
}

inline const std::vector<Entry>& recoil()
{
    static const std::vector<Entry> v {
        { "Blackout", "Total silence for the kick's body, then a fast smooth return. Pair it with FIT LENGTH.",
          { { 0, 0 }, { 0.42f, 0 }, { 0.56f, 1, 2.2f, true }, { 1, 1 } } },
        { "808 Tail", "Clears the transient fully, holds the 808 low through the kick's body, then lets it bloom.",
          { { 0, 0 }, { 0.07f, 0 }, { 0.18f, 0.35f, 1.5f }, { 0.42f, 0.35f }, { 0.74f, 1, 1.6f, true }, { 1, 1 } } },
        { "Recoil", "Comes back slightly above the original level, then settles: the bass hits back after each kick.",
          { { 0, 0 }, { 0.13f, 0 }, { 0.36f, 1.12f, 2, true }, { 0.62f, 1, 1.5f, true }, { 1, 1 } } },
        { "Click Gap", "A very short duck that only clears room for the kick's click. For vocals and top end.",
          { { 0, 0 }, { 0.015f, 0 }, { 0.09f, 1, 1.8f, true }, { 1, 1 } } },
    };
    return v;
}

// "shape" parameter choices: FACTORY, RECOIL, then Custom
inline int numBuiltIn() { return (int) (factory().size() + recoil().size()); }
inline int customIndex() { return numBuiltIn(); }
inline const Entry* builtIn (int index)
{
    if (index < 0) return nullptr;
    if (index < (int) factory().size()) return &factory()[(size_t) index];
    index -= (int) factory().size();
    if (index < (int) recoil().size()) return &recoil()[(size_t) index];
    return nullptr;
}
inline juce::StringArray choiceNames()
{
    juce::StringArray a;
    for (auto& e : factory()) a.add (e.name);
    for (auto& e : recoil()) a.add (e.name);
    a.add ("Custom");
    return a;
}

constexpr int kMaxNodes = 32;

inline std::vector<Node> sanitise (std::vector<Node> n)
{
    std::sort (n.begin(), n.end(), [] (const Node& a, const Node& b) { return a.x < b.x; });
    if (n.size() > (size_t) kMaxNodes) n.resize ((size_t) kMaxNodes);
    for (auto& p : n) { p.x = juce::jlimit (0.0f, 1.0f, p.x); p.y = juce::jlimit (0.0f, 1.2f, p.y); p.bend = juce::jlimit (-6.0f, 6.0f, p.bend); }
    if (n.empty()) n = { { 0, 0 }, { 0.1f, 0 }, { 0.5f, 1, 2, true }, { 1, 1 } };
    n.front().x = 0.0f;
    if (n.size() < 2) n.push_back ({ 1, n.front().y });
    n.back().x = juce::jmax (n.back().x, n.front().x);
    return n;
}

inline juce::ValueTree toTree (const std::vector<Node>& nodes)
{
    juce::ValueTree t ("SHAPE");
    for (auto& p : nodes)
    {
        juce::ValueTree c ("N");
        c.setProperty ("x", p.x, nullptr); c.setProperty ("y", p.y, nullptr);
        c.setProperty ("b", p.bend, nullptr); c.setProperty ("s", p.s, nullptr);
        t.appendChild (c, nullptr);
    }
    return t;
}

inline std::vector<Node> fromTree (const juce::ValueTree& t)
{
    std::vector<Node> v;
    for (const auto& c : t)
        v.push_back ({ (float) c.getProperty ("x", 0.0), (float) c.getProperty ("y", 1.0), (float) c.getProperty ("b", 0.0), (bool) c.getProperty ("s", false) });
    return v;
}

// ---- user shapes: <RECOIL>/Shapes/<name>.rcshape
inline juce::File userDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("RECOIL").getChildFile ("Shapes");
}
inline juce::StringArray userNames()
{
    juce::StringArray a;
    for (auto& f : userDir().findChildFiles (juce::File::findFiles, false, "*.rcshape")) a.add (f.getFileNameWithoutExtension());
    a.sortNatural();
    return a;
}
inline std::vector<Node> loadUser (const juce::String& name)
{
    if (auto xml = juce::parseXML (userDir().getChildFile (name + ".rcshape")))
        return sanitise (fromTree (juce::ValueTree::fromXml (*xml)));
    return {};
}
inline bool saveUser (const juce::String& name, const std::vector<Node>& nodes)
{
    const auto legal = juce::File::createLegalFileName (name.trim());
    if (legal.isEmpty() || ! userDir().createDirectory().wasOk()) return false;
    if (auto xml = toTree (nodes).createXml()) return xml->writeTo (userDir().getChildFile (legal + ".rcshape"));
    return false;
}
inline bool removeUser (const juce::String& name) { return userDir().getChildFile (name + ".rcshape").deleteFile(); }
} // namespace shapes
